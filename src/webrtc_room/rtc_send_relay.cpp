#include "rtc_send_relay.hpp"
#include "port_generator.hpp"
#include "config/config.hpp"
#include "utils/timeex.hpp"
#include "utils/byte_crypto.hpp"
#include "utils/event_log.hpp"
#include "net/rtprtcp/rtprtcp_pub.hpp"
#include "net/rtprtcp/rtcp_pspli.hpp"
#include "net/rtprtcp/rtcp_rr.hpp"
#include "config/config.hpp"

extern std::unique_ptr<cpp_streamer::EventLog> g_rtc_stream_log;

namespace cpp_streamer {
RtcSendRelay::RtcSendRelay(const std::string& room_id, 
        const std::string& pusher_user_id,
        const std::string& remote_ip,
        uint16_t remote_port,
        MediaPushPullEventI* media_event_cb,
        uv_loop_t* loop, Logger* logger):TimerInterface(300)
{
    room_id_ = room_id;
    pusher_user_id_ = pusher_user_id;
    remote_ip_ = remote_ip;
    remote_port_ = remote_port;
    media_event_cb_ = media_event_cb;
    loop_ = loop;
    logger_ = logger;

    send_discard_percent_ = Config::Instance().relay_cfg_.send_discard_percent_;

    udp_listen_ip_ = Config::Instance().relay_cfg_.relay_server_ip_;
    udp_listen_port_ = PortGenerator::Instance()->GeneratePort();

    udp_client_ptr_.reset(new UdpClient(loop_, 
        this, logger_, udp_listen_ip_.c_str(), udp_listen_port_));
    udp_client_ptr_->TryRead();
    LogInfof(logger_, "RtcSendRelay construct, roomId:%s, pushUserId:%s, \
remoteIp:%s, remotePort:%u, udpListenIp:%s, udpListenPort:%u", 
      room_id_.c_str(), pusher_user_id_.c_str(), 
      remote_ip_.c_str(), remote_port_, 
      udp_listen_ip_.c_str(), udp_listen_port_);

    last_alive_ms_ = now_millisec();
    StartTimer();
}

RtcSendRelay::~RtcSendRelay() {
    udp_client_ptr_->Close();
    udp_client_ptr_.reset();

    StopTimer();
    LogInfof(logger_, "RtcSendRelay destruct, roomId:%s, pushUserId:%s", 
      room_id_.c_str(), pusher_user_id_.c_str());
}

void RtcSendRelay::SendRtpPacket(RtpPacket* in_rtp_pkt) {
    try {
        RtpPacket* rtp_packet = in_rtp_pkt;
        UdpTuple remote_address(remote_ip_, remote_port_);

        uint32_t ssrc = rtp_packet->GetSsrc();
        bool found = false;

        auto it = ssrc2send_session_.find(ssrc);
        if (it != ssrc2send_session_.end()) {
            found = true;
            bool r = it->second->SendRtpPacket(rtp_packet);
            if (!r) {
                LogErrorf(logger_, "RtcSendRelay::SendRtpPacket send session send rtp packet failed, ssrc:%u", ssrc);
                return;
            }
        } else {
            auto rtx_it = rtx_ssrc2send_session_.find(ssrc);
            if (rtx_it != rtx_ssrc2send_session_.end()) {
                found = true;
                bool r = rtx_it->second->SendRtpPacket(rtp_packet);
                if (!r) {
                    LogErrorf(logger_, "RtcSendRelay::SendRtpPacket rtx send session send rtp packet failed, ssrc:%u", ssrc);
                    return;
                }
            }
        }
        if (!found) {
            return;
        }

        OnTransportSendRtp(rtp_packet->GetData(), rtp_packet->GetDataLength());
    } catch (const CppStreamException& ex) {
        LogErrorf(logger_, "RtcSendRelay::SendRtpPacket exception:%s", ex.what());
    }
}

void RtcSendRelay::AddPushInfo(const PushInfo& push_info) {
    push_infos_.emplace(std::make_pair(push_info.pusher_id_, push_info));
    std::shared_ptr<RtpSendSession> send_session_ptr = 
        std::make_shared<RtpSendSession>(push_info.param_,
            room_id_,
            "",//puller_user_id
            pusher_user_id_,
            this,
            loop_,
            logger_);

    ssrc2send_session_.emplace(std::make_pair(push_info.param_.ssrc_, send_session_ptr));
    if (push_info.param_.rtx_ssrc_ != 0) {
        rtx_ssrc2send_session_.emplace(std::make_pair(push_info.param_.rtx_ssrc_, send_session_ptr));
    }
    return;
}

bool RtcSendRelay::OnTimer() {
    int64_t now_ms = now_millisec();
    for (auto& it : ssrc2send_session_) {
        it.second->OnTimer(now_ms);
        if (last_statics_ms_ < 0) {
            last_statics_ms_ = now_ms;
        }
        if (now_ms - last_statics_ms_ > 5000) {
            last_statics_ms_ = now_ms;
            if (g_rtc_stream_log) {
                StreamStatics& stats = it.second->GetSendStatics();
                const auto rtp_params = it.second->GetRtpSessionParam();
                size_t pps = 0;
                size_t bps = stats.BytesPerSecond(now_millisec(), pps);
                size_t kbps = bps * 8 / 1000;
                json evt_json;
                evt_json["event"] = "relay_send";
                evt_json["room_id"] = room_id_;
                evt_json["pusher_user_id"] = pusher_user_id_;
                evt_json["ssrc"] = it.first;
                evt_json["av_type"] = avtype_tostring(rtp_params.av_type_);
                evt_json["bytes_sent"] = stats.GetBytes();
                evt_json["packets_sent"] = stats.GetCount();
                evt_json["kbps"] = kbps;
                evt_json["pps"] = pps;

                g_rtc_stream_log->Log("relay_send", evt_json);
            }
        }
    }

    return timer_running_;
}

void RtcSendRelay::OnWrite(size_t sent_size, UdpTuple address) {
    //TODO
}

void RtcSendRelay::OnRead(const char* data, size_t data_size, UdpTuple address) {
    if (data == nullptr || data_size == 0) {
        return;
    }

    if (IsRtcp((uint8_t*)data, data_size)) {
        HandleRtcpPacket((uint8_t*)data, data_size, address);
    } else if (IsRtp((uint8_t*)data, data_size)) {
        LogErrorf(logger_, "RtcSendRelay::OnRead should not receive rtp packet, len:%zu", data_size);
    } else {
        LogErrorf(logger_, "RtcRecvRelay::OnRead unknown packet type, len:%zu", data_size);
        return;
    }
}

void RtcSendRelay::HandleRtcpPacket(const uint8_t* data, size_t data_size, UdpTuple address) {
    int left_len = static_cast<int>(data_size);
    uint8_t* p = const_cast<uint8_t*>(data);

    while (left_len > 0) {
        RtcpCommonHeader* rtcp_hdr = reinterpret_cast<RtcpCommonHeader*>(p);
        size_t item_total = GetRtcpLength(rtcp_hdr);

        switch (rtcp_hdr->packet_type)
        {
            case RTCP_RR:
            {
                //todo: handle rtcp rr packet
                HandleRtcpRrPacket(p, item_total);
                break;
            }
            case RTCP_PSFB:
            {
                //todo: handle rtcp psfb packet
                HandleRtcpPsfbPacket(p, item_total);
                break;
            }
            case RTCP_RTPFB:
            {
                HandleRtcpRtpfbPacket(p, item_total);
                break;
            }
            default:
            {
                LogErrorf(logger_, "RtcSendRelay::HandleRtcpPacket unknown RTCP packet type:%u", rtcp_hdr->packet_type);
                break;
            }
        }
        p += item_total;
        left_len -= (int)item_total;
    }
}

void RtcSendRelay::HandleRtcpRtpfbPacket(const uint8_t* data, size_t len) {
    if (len <=sizeof(RtcpFbCommonHeader)) {
        return;
    }
    try {
        auto fb_hdr = (RtcpFbCommonHeader*)(const_cast<uint8_t*>(data));
        switch (fb_hdr->fmt)
        {
            case FB_RTP_NACK:
            {
                LogDebugf(logger_, "Handle RTCP RTPFB NACK, room_id:%s, user_id:%s, len:%zu",
                    room_id_.c_str(), pusher_user_id_.c_str(), len);
                RtcpFbNack* nack_pkt = RtcpFbNack::Parse(const_cast<uint8_t*>(data), len);
                if (!nack_pkt) {
                    LogErrorf(logger_, "Parse RTCP RTPFB NACK packet failed, room_id:%s, user_id:%s, len:%zu",
                        room_id_.c_str(), pusher_user_id_.c_str(), len);
                    return;
                }
                uint32_t ssrc = nack_pkt->GetMediaSsrc();
                
                auto it = ssrc2send_session_.find(ssrc);
                if (it != ssrc2send_session_.end()) {
                    it->second->RecvRtcpFbNack(nack_pkt);
                } else {
                    LogErrorf(logger_, "Cannot find send session for RTCP RTPFB NACK ssrc:%u, room_id:%s, user_id:%s, len:%zu",
                        ssrc, room_id_.c_str(), pusher_user_id_.c_str(), len);
                }
                delete nack_pkt;
                break;
            }
            default:
            {
                LogErrorf(logger_, "Unknown RTCP RTPFB fmt:%d, room_id:%s, user_id:%s, len:%zu",
                    fb_hdr->fmt, room_id_.c_str(), pusher_user_id_.c_str(), len);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpRtpfbPacket exception:%s, room_id:%s, user_id:%s, len:%zu",
            e.what(), room_id_.c_str(), pusher_user_id_.c_str(), len);
    }
    
    return;
}
void RtcSendRelay::HandleRtcpRrPacket(const uint8_t* data, size_t len) {
    try {
        RtcpRrPacket* rr_packet = RtcpRrPacket::Parse((uint8_t*)data, len);
        if (!rr_packet) {
            LogErrorf(logger_, "RtcSendRelay::HandleRtcpRrPacket parse rtcp rr packet failed, len:%zu", len);
            return;
        }
        auto rr_blocks = rr_packet->GetRrBlocks();
        for (auto rr_block : rr_blocks) {
            uint32_t reportee_ssrc = rr_block.GetReporteeSsrc();
            auto it = ssrc2send_session_.find(reportee_ssrc);
            if (it != ssrc2send_session_.end()) {
                it->second->RecvRtcpRrBlock(rr_block);
            } else {
                LogErrorf(logger_, "RtcSendRelay::HandleRtcpRrPacket cannot find send session for rtcp rr reportee ssrc:%u", reportee_ssrc);
                break;
            }
        }
        delete rr_packet;
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RtcSendRelay::HandleRtcpRrPacket exception:%s", e.what());
    }
    
}

void RtcSendRelay::HandleRtcpPsfbPacket(const uint8_t* data, size_t len) {
    if (len <=sizeof(RtcpFbCommonHeader)) {
        return;
    }
    try {
        auto fb_hdr = (RtcpFbCommonHeader*)(const_cast<uint8_t*>(data));
        switch (fb_hdr->fmt)
        {
            case FB_PS_PLI:
            {
                LogInfof(logger_, "Handle RTCP PSFB PLI, room_id:%s, user_id:%s, len:%zu",
                    room_id_.c_str(), pusher_user_id_.c_str(), len);
                RtcpPsPli* pspli_pkt = RtcpPsPli::Parse(const_cast<uint8_t*>(data), len);
                if (!pspli_pkt) {
                    LogErrorf(logger_, "Parse RTCP PSFB PLI packet failed, room_id:%s, user_id:%s, len:%zu",
                        room_id_.c_str(), pusher_user_id_.c_str(), len);
                    return;
                }
                uint32_t ssrc = pspli_pkt->GetMediaSsrc();
                
                std::string pusher_id;
                for (const auto& it : push_infos_) {
                    if (it.second.param_.ssrc_ == ssrc) {
                        pusher_id = it.second.pusher_id_;
                        break;;
                    }
                }
                if (pusher_id.empty()) {
                    LogErrorf(logger_, "Cannot find pusher id for RTCP PSFB PLI ssrc:%u, room_id:%s, user_id:%s, len:%zu",
                        ssrc, room_id_.c_str(), pusher_user_id_.c_str(), len);
                    delete pspli_pkt;
                    return;
                }
                if (media_event_cb_) {
                    media_event_cb_->OnKeyFrameRequest(pusher_id, 
                        "remote_user_id",//puller_user_id
                        pusher_user_id_,
                        ssrc);
                }
                delete pspli_pkt;
            }
            case FB_PS_AFB:
            {
                LogDebugf(logger_, "Handle RTCP PSFB AFB, room_id:%s, user_id:%s, len:%zu",
                    room_id_.c_str(), pusher_user_id_.c_str(), len);
                break;
            }
            default:
            {
                LogErrorf(logger_, "Unknown RTCP PSFB fmt:%d, room_id:%s, user_id:%s, len:%zu",
                    fb_hdr->fmt, room_id_.c_str(), pusher_user_id_.c_str(), len);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRtcpPsfbPacket exception:%s, room_id:%s, user_id:%s, len:%zu",
            e.what(), room_id_.c_str(), pusher_user_id_.c_str(), len);
    }
    
    return;
}

bool RtcSendRelay::IsConnected() {
    if (udp_client_ptr_ == nullptr) {
        return false;
    }
    if (remote_ip_.empty() || remote_port_ == 0) {
        return false;
    }
    return true;
}

void RtcSendRelay::OnTransportSendRtp(uint8_t* data, size_t sent_size) {
    if (DiscardPacketByPercent(send_discard_percent_)) {
        return;
    }
    last_alive_ms_ = now_millisec();
    UdpTuple remote_address(remote_ip_, remote_port_);
    
    udp_client_ptr_->Write((const char*)data, sent_size, remote_address);
}

void RtcSendRelay::OnTransportSendRtcp(uint8_t* data, size_t sent_size) {
    UdpTuple remote_address(remote_ip_, remote_port_);

    udp_client_ptr_->Write((const char*)data, sent_size, remote_address);
}

bool RtcSendRelay::DiscardPacketByPercent(uint32_t percent) {
    if (percent > 0) {
        uint32_t rand_val = ByteCrypto::GetRandomUint(0, 100);
        if (rand_val <= percent) {
            return true;
        }
    }
    return false;
}

bool RtcSendRelay::IsAlive() {
    const int64_t HEARTBEAT_TIMEOUT_MS = 40*1000;
    int64_t now_ms = now_millisec();
    return (now_ms - last_alive_ms_) <= HEARTBEAT_TIMEOUT_MS;
}
} // namespace cpp_streamer
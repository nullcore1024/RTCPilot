#include "rtc_recv_relay.hpp"
#include "port_generator.hpp"
#include "utils/uuid.hpp"
#include "utils/timeex.hpp"
#include "utils/byte_crypto.hpp"
#include "utils/event_log.hpp"
#include "utils/json.hpp"
#include "config/config.hpp"
#include "net/rtprtcp/rtp_packet.hpp"
#include "net/rtprtcp/rtprtcp_pub.hpp"
#include "net/rtprtcp/rtcp_sr.hpp"
#include "net/rtprtcp/rtcp_pspli.hpp"

extern std::unique_ptr<cpp_streamer::EventLog> g_rtc_stream_log;

namespace cpp_streamer {

using json = nlohmann::json;

RtcRecvRelay::RtcRecvRelay(const std::string& room_id, const std::string& pusher_user_id,
        PacketFromRtcPusherCallbackI* packet2room_cb,
        uv_loop_t* loop, Logger* logger): TimerInterface(500)
                                        , logger_(logger)
{
    loop_ = loop;
    room_id_ = room_id;
    pusher_user_id_ = pusher_user_id;
    packet2room_cb_ = packet2room_cb;
    udp_port_ = PortGenerator::Instance()->GeneratePort();
    listen_ip_ = Config::Instance().relay_cfg_.relay_server_ip_;

    recv_discard_percent_ = Config::Instance().relay_cfg_.recv_discard_percent_;
    udp_client_ptr_.reset(new UdpClient(loop_, this, logger_, listen_ip_.c_str(), udp_port_));
    udp_client_ptr_->TryRead();

    last_alive_ms_ = now_millisec();

    StartTimer();
    LogInfof(logger_, "RtcRecvRelay construct, roomId:%s, pushUserId:%s, udpListenIp:%s, udpListenPort:%u", 
      room_id_.c_str(), pusher_user_id_.c_str(), listen_ip_.c_str(), udp_port_);
}

RtcRecvRelay::~RtcRecvRelay() {
    udp_client_ptr_->Close();
    udp_client_ptr_.reset();

    StopTimer();
    LogInfof(logger_, "RtcRecvRelay destruct, roomId:%s, pushUserId:%s", 
      room_id_.c_str(), pusher_user_id_.c_str());
}

bool RtcRecvRelay::IsAlive() {
    const int64_t HEARTBEAT_TIMEOUT_MS = 40*1000; // 30 seconds
    int64_t now_ms = now_millisec();
    if (now_ms - last_alive_ms_ > HEARTBEAT_TIMEOUT_MS) {
        return false;
    }
    return true;
}

int RtcRecvRelay::AddVirtualPusher(const PushInfo& push_info) {
    push_infos_.emplace(std::make_pair(push_info.pusher_id_, push_info));
    ssrc2push_infos_.emplace(std::make_pair(push_info.param_.ssrc_, push_info));
    json push_info_json = json::object();
    push_info.DumpJson(push_info_json);
    LogInfof(logger_, "RtcRecvRelay::AddVirtualPusher, roomId:%s, pushUserId:%s, pusherId:%s, push_info:%s, ssrc2push_infos_ size:%zu", 
      room_id_.c_str(), pusher_user_id_.c_str(), 
      push_info.pusher_id_.c_str(), 
      push_info_json.dump().c_str(), ssrc2push_infos_.size());
    auto recv_session_ptr = std::make_shared<RtpRecvSession>(push_info.param_,
        room_id_,
        pusher_user_id_,
        this,
        loop_,
        logger_);
    ssrc2recv_session_.emplace(std::make_pair(push_info.param_.ssrc_, recv_session_ptr));

    if (push_info.param_.rtx_ssrc_ != 0) {
        rtx_ssrc2recv_session_.emplace(std::make_pair(push_info.param_.rtx_ssrc_, recv_session_ptr));
    }
    return 0;
}

bool RtcRecvRelay::DiscardPacketByPercent(uint32_t percent) {
    if (percent == 0) {
        return false;
    }
    uint32_t rand_val = ByteCrypto::GetRandomUint(0, 100);
    if (rand_val <= percent) {
        return true;
    }
    return false;
}

void RtcRecvRelay::OnWrite(size_t sent_size, UdpTuple address) {
    //TODO
}

void RtcRecvRelay::OnRead(const char* data, size_t data_size, UdpTuple address) {
    if (DiscardPacketByPercent(recv_discard_percent_)) {
        return;
    }

    if (data == nullptr || data_size == 0) {
        return;
    }
    remote_address_ = address;
    if (IsRtcp((uint8_t*)data, data_size)) {
        HandleRtcpPacket((uint8_t*)data, data_size, address);
    } else if (IsRtp((uint8_t*)data, data_size)) {
        last_alive_ms_ = now_millisec();
        HandleRtpPacket((uint8_t*)data, data_size, address);
    } else {
        LogErrorf(logger_, "RtcRecvRelay::OnRead unknown packet type, len:%zu", data_size);
        return;
    }
}

void RtcRecvRelay::HandleRtpPacket(const uint8_t* data, size_t data_size, UdpTuple address) {
    try {
        bool repeat = false;
        RtpPacket* rtp_packet = RtpPacket::Parse((uint8_t*)data, data_size);
        uint32_t ssrc = rtp_packet->GetSsrc();
        auto it = ssrc2recv_session_.find(ssrc);;
        if (it != ssrc2recv_session_.end()) {
            bool r = it->second->ReceiveRtpPacket(rtp_packet);
            if (!r) {
                LogErrorf(logger_, "RtcRecvRelay::OnRead recv session receive rtp packet failed, ssrc:%u", ssrc);
                delete rtp_packet;
                return;
            }
        } else {
            auto rtx_it = rtx_ssrc2recv_session_.find(ssrc);
            if (rtx_it != rtx_ssrc2recv_session_.end()) {
                bool r = rtx_it->second->ReceiveRtxPacket(rtp_packet, repeat);
                if (!r) {
                    LogErrorf(logger_, "RtcRecvRelay::OnRead recv session receive rtx packet failed, ssrc:%u", ssrc);
                    delete rtp_packet;
                    return;
                }
            }
            if (repeat) {
                return;
            }
            if (rtp_packet->GetSsrc() == ssrc) {
                return;
            }
        }

        if (rtp_packet->GetPayloadLength() == 0) {
            return;
        }
        ssrc = rtp_packet->GetSsrc();
        LogDebugf(logger_, "RtcRecvRelay::OnRead received rtp packet:%s", rtp_packet->Dump().c_str());
        if (packet2room_cb_) {
            auto it = ssrc2push_infos_.find(ssrc);;
            if (it == ssrc2push_infos_.end()) {
                LogErrorf(logger_, "RtcRecvRelay::OnRead no push info for ssrc:%u, ssrc2push_infos_.size:%zu", 
                    ssrc, ssrc2push_infos_.size());
                delete rtp_packet;
                return;
            }
            packet2room_cb_->OnRtpPacketFromRemoteRtcPusher(pusher_user_id_, 
                it->second.pusher_id_,
                rtp_packet);
        };
        delete rtp_packet;    
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RtcRecvRelay::OnRead exception:%s", e.what());
    }
}

void RtcRecvRelay::HandleRtcpPacket(const uint8_t* data, size_t data_size, UdpTuple address) {
    int left_len = static_cast<int>(data_size);
    uint8_t* p = const_cast<uint8_t*>(data);

    while (left_len > 0) {
        RtcpCommonHeader* rtcp_hdr = reinterpret_cast<RtcpCommonHeader*>(p);
        size_t item_total = GetRtcpLength(rtcp_hdr);

        switch (rtcp_hdr->packet_type)
        {
            case RTCP_SR:
            {
                HandleRtcpSrPacket(p, item_total);
                break;
            }
            default:
            {
                LogErrorf(logger_, "RtcRecvRelay::HandleRtcpPacket unknown RTCP packet type:%u", rtcp_hdr->packet_type);
                break;
            }
        }
        p += item_total;
        left_len -= (int)item_total;
    }
}

void RtcRecvRelay::HandleRtcpSrPacket(const uint8_t* data, size_t data_size) {
    try {
        RtcpSrPacket* sr_packet = RtcpSrPacket::Parse((uint8_t*)data, data_size);
        uint32_t ssrc = sr_packet->GetSsrc();
        auto it = ssrc2recv_session_.find(ssrc);
        if (it != ssrc2recv_session_.end()) {
            int ret = it->second->HandleRtcpSrPacket(sr_packet);
            if (ret < 0) {
                LogErrorf(logger_, "RtcRecvRelay::HandleRtcpSrPacket recv session handle rtcp sr packet failed, ssrc:%u", ssrc);
                delete sr_packet;
                return;
            }
        }
        delete sr_packet;
    } catch(const std::exception& e) {
        LogErrorf(logger_, "RtcRecvRelay::HandleRtcpSrPacket exception:%s", e.what());
    }
}
//implement TransportSendCallbackI
bool RtcRecvRelay::IsConnected() {
    if (!udp_client_ptr_) {
        return false;
    }
    if (remote_address_.ip_address.empty() || remote_address_.port == 0) {
        return false;
    }
    return true;
}

void RtcRecvRelay::OnTransportSendRtp(uint8_t* data, size_t sent_size) {
    LogErrorf(logger_, "RtcRecvRelay::OnTransportSendRtp should not be called");
    assert(0);
}

void RtcRecvRelay::OnTransportSendRtcp(uint8_t* data, size_t sent_size) {
    if (data == nullptr || sent_size == 0) {
        return;
    }
    if (remote_address_.ip_address.empty() || remote_address_.port == 0) {
        LogErrorf(logger_, "RtcRecvRelay::OnTransportSendRtcp no remote address");
        return;
    }
    udp_client_ptr_->Write((const char*)data, sent_size, remote_address_);
}

MEDIA_PKT_TYPE RtcRecvRelay::GetMediaType(const std::string& pusher_id) {
    MEDIA_PKT_TYPE media_type = MEDIA_UNKNOWN_TYPE;
    auto it = push_infos_.find(pusher_id);;
    if (it != push_infos_.end()) {
        media_type = it->second.param_.av_type_;
    }
    return media_type;
}

bool RtcRecvRelay::GetPushInfo(const std::string& pusher_id, PushInfo& push_info) {
    auto it = push_infos_.find(pusher_id);;
    if (it != push_infos_.end()) {
        push_info = it->second;
        return true;
    }
    return false;
}

void RtcRecvRelay::RequestKeyFrame(uint32_t ssrc) {
    auto push_info_it = ssrc2push_infos_.find(ssrc);
    if (push_info_it == ssrc2push_infos_.end()) {
        LogErrorf(logger_, "RtcRecvRelay::RequestKeyFrame no push info for ssrc:%u", ssrc);
        return;
    }
    PushInfo& push_info = push_info_it->second;

    std::unique_ptr<RtcpPsPli> pspli_pkt = std::make_unique<RtcpPsPli>();

    pspli_pkt->SetSenderSsrc(0); //0 means server
    pspli_pkt->SetMediaSsrc(ssrc);
    
    LogInfof(logger_, "RtcRecvRelay RequestKeyFrame, room_id:%s, pusher_user_id_:%s, pusher_id:%s, ssrc:%u",
        room_id_.c_str(), pusher_user_id_.c_str(), push_info.pusher_id_.c_str(), ssrc);
    OnTransportSendRtcp(pspli_pkt->GetData(), pspli_pkt->GetDataLen());
}

//implement TimerInterface
bool RtcRecvRelay::OnTimer() {
    int64_t now_ms = now_millisec();
    if (last_statics_ms_ < 0) {
        last_statics_ms_ = now_ms;
        return true;
    }
    if (now_ms - last_statics_ms_ < 5000) {
        return true;
    }
    last_statics_ms_ = now_ms;
    
    for (auto& it : ssrc2recv_session_) {
        StreamStatics& stats = it.second->GetRecvStatics();
        size_t pps = 0;
        size_t bps = stats.BytesPerSecond(now_millisec(), pps);
        size_t kbps = bps * 8 / 1000;
        const auto rtp_params = it.second->GetRtpSessionParam();

        LogDebugf(logger_, "++++>rtc recv relay RecvStatics, room_id:%s, pusher_user_id_:%s, \
            ssrc:%u, av_type:%s, kbps:%zu, pps:%zu, total_bytes:%zu, total_pkts:%zu",
            room_id_.c_str(), pusher_user_id_.c_str(),
            it.first,
            avtype_tostring(rtp_params.av_type_).c_str(),
            kbps,
            pps,
            stats.GetBytes(),
            stats.GetCount());

        // log to event log
        if (g_rtc_stream_log) {
            json evt_data;
            evt_data["event"] = "relay_recv";
            evt_data["room_id"] = room_id_;
            evt_data["pusher_user_id"] = pusher_user_id_;
            evt_data["ssrc"] = it.first;
            evt_data["media_type"] = avtype_tostring(rtp_params.av_type_);
            evt_data["kbps"] = kbps;
            evt_data["pps"] = pps;
            evt_data["total_bytes"] = stats.GetBytes();
            evt_data["total_pkts"] = stats.GetCount();
            g_rtc_stream_log->Log("relay_recv", evt_data);
        }
    }
    return true;
}

} // namespace cpp_streamer
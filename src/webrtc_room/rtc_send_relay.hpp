#ifndef RTC_SEND_RELAY_HPP
#define RTC_SEND_RELAY_HPP
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "net/udp/udp_client.hpp"
#include "net/rtprtcp/rtp_packet.hpp"
#include "rtc_info.hpp"
#include "rtp_send_session.hpp"
#include <uv.h>
#include <map>

namespace cpp_streamer {

class RtcSendRelay : public TimerInterface, public UdpSessionCallbackI, public TransportSendCallbackI
{
public:
    RtcSendRelay(const std::string& room_id, 
        const std::string& pusher_user_id,
        const std::string& remote_ip,
        uint16_t remote_port,
        MediaPushPullEventI* media_event_cb,
        uv_loop_t* loop, Logger* logger);
    virtual ~RtcSendRelay();

public:
    void SendRtpPacket(RtpPacket* rtp_packet);
    std::string GetPusherId() { return pusher_user_id_; }
    std::string GetRoomId() { return room_id_; }
    void AddPushInfo(const PushInfo& push_info);
    bool IsAlive();

public://implement UdpSessionCallbackI
    virtual void OnWrite(size_t sent_size, UdpTuple address) override;
    virtual void OnRead(const char* data, size_t data_size, UdpTuple address) override;

public://implement TimerInterface
    virtual bool OnTimer() override;

public://implement TransportSendCallbackI
    virtual bool IsConnected() override;
    virtual void OnTransportSendRtp(uint8_t* data, size_t sent_size) override;
    virtual void OnTransportSendRtcp(uint8_t* data, size_t sent_size) override;
    
private:
    void HandleRtcpPacket(const uint8_t* data, size_t data_size, UdpTuple address);
    void HandleRtcpPsfbPacket(const uint8_t* data, size_t len);
    void HandleRtcpRrPacket(const uint8_t* data, size_t len);
    void HandleRtcpRtpfbPacket(const uint8_t* data, size_t len);
    bool DiscardPacketByPercent(uint32_t percent);

private:
    std::string room_id_;
    std::string pusher_user_id_;
    std::string remote_ip_;
    uint16_t    remote_port_ = 0;
    MediaPushPullEventI* media_event_cb_ = nullptr;
    uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;

private:
    std::string udp_listen_ip_;
    uint16_t    udp_listen_port_ = 0;
    std::unique_ptr<UdpClient> udp_client_ptr_;

private:
    std::map<std::string, PushInfo> push_infos_;// pusher_id -> PushInfo
    std::map<uint32_t, std::shared_ptr<RtpSendSession>> ssrc2send_session_;// ssrc -> RtpSendSession
    std::map<uint32_t, std::shared_ptr<RtpSendSession>> rtx_ssrc2send_session_;// rtx_ssrc -> RtpSendSession

private:
    int64_t last_alive_ms_ = -1;

private:
    uint32_t send_discard_percent_ = 0;

private:
    int64_t last_statics_ms_ = 0;
};

}

#endif // RTC_SEND_RELAY_HPP
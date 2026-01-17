#ifndef RTC_RELAY_HPP
#define RTC_RELAY_HPP
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "utils/av/av.hpp"
#include "utils/json.hpp"
#include "utils/timer.hpp"
#include "rtc_info.hpp"
#include "net/udp/udp_client.hpp"
#include "rtp_recv_session.hpp"
#include <memory>
#include <string>
#include <map>
#include <uv.h>

namespace cpp_streamer {

class PacketFromRtcPusherCallbackI;
class RtcRecvRelay : public UdpSessionCallbackI, public TransportSendCallbackI, public TimerInterface
{
public:
    RtcRecvRelay(const std::string& room_id, const std::string& pusher_user_id,
        PacketFromRtcPusherCallbackI* packet2room_cb,
        uv_loop_t* loop, Logger* logger);
    virtual ~RtcRecvRelay();

    int AddVirtualPusher(const PushInfo& push_info);

public:
    MEDIA_PKT_TYPE GetMediaType(const std::string& pusher_id);
    std::string GetPushUserId() { return pusher_user_id_; }
    std::string GetRoomId() { return room_id_; }
    std::string GetListenUdpIp() { return listen_ip_; }
    uint16_t    GetListenUdpPort() { return udp_port_; }
    bool GetPushInfo(const std::string& pusher_id, PushInfo& push_info);
    void RequestKeyFrame(uint32_t ssrc);
    bool IsAlive();

public://implement TransportSendCallbackI
    virtual bool IsConnected() override;
    virtual void OnTransportSendRtp(uint8_t* data, size_t sent_size) override;
    virtual void OnTransportSendRtcp(uint8_t* data, size_t sent_size) override;

protected://implement UdpSessionCallbackI
    virtual void OnWrite(size_t sent_size, UdpTuple address) override;
    virtual void OnRead(const char* data, size_t data_size, UdpTuple address) override;

protected://implement TimerInterface
    virtual bool OnTimer() override;

private:
    void HandleRtpPacket(const uint8_t* data, size_t data_size, UdpTuple address);
    void HandleRtcpPacket(const uint8_t* data, size_t data_size, UdpTuple address);
    void HandleRtcpSrPacket(const uint8_t* data, size_t data_size);
    bool DiscardPacketByPercent(uint32_t percent);
    
private:
    std::string room_id_;
    std::string pusher_user_id_;
    PacketFromRtcPusherCallbackI* packet2room_cb_ = nullptr;
    uv_loop_t* loop_ = nullptr;
    Logger* logger_ = nullptr;

private:
    std::map<std::string, PushInfo> push_infos_;
    std::map<uint32_t, PushInfo> ssrc2push_infos_;
    std::map<uint32_t, std::shared_ptr<RtpRecvSession>> ssrc2recv_session_;
    std::map<uint32_t, std::shared_ptr<RtpRecvSession>> rtx_ssrc2recv_session_;

private:
    uint16_t udp_port_ = 0;
    std::string listen_ip_;
    std::unique_ptr<UdpClient> udp_client_ptr_;
    UdpTuple remote_address_;

private:
    int64_t last_alive_ms_ = -1;

private:
    uint32_t recv_discard_percent_ = 0;

private:
    int64_t last_statics_ms_ = -1;
};

}

#endif
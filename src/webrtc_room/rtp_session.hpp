#ifndef RTP_SESSION_HPP
#define RTP_SESSION_HPP
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "utils/av/av.hpp"
#include "utils/json.hpp"
#include "net/rtprtcp/rtp_packet.hpp"
#include "format/rtc_sdp/rtc_sdp.hpp"
#include "nack_generator.hpp"
#include "udp_transport.hpp"
#include "rtc_info.hpp"

#include <uv.h>
#include <memory>

namespace cpp_streamer {

class RtpSession
{
public:
    RtpSession(const RtpSessionParam& param,
        const std::string& room_id,
        const std::string& user_id,
        TransportSendCallbackI* cb,
        uv_loop_t* loop, Logger* logger);
    virtual ~RtpSession();

public:
    const RtpSessionParam& GetRtpSessionParam() const { return param_; }
    
protected:
    void InitSeq(uint16_t seq);
    bool UpdateSeq(RtpPacket* rtp_pkt);
    int64_t GetExpectedPackets() const;

protected:
    RtpSessionParam param_;
    Logger* logger_ = nullptr;
    std::string room_id_;
    std::string user_id_;

protected:
    TransportSendCallbackI* transport_cb_ = nullptr;

protected:
    bool first_pkt_ = true;
	uint32_t max_packet_ts_ = 0;
	uint64_t max_packet_ms_ = 0;
    int64_t last_pkt_ms_ = 0;
    int64_t last_rtp_ts_ = 0;
    uint32_t jitter_q4_  = 0;
    uint32_t jitter_     = 0;

protected:
    uint32_t cycles_   = 0;
	uint32_t base_seq_ = 0;
	uint16_t max_seq_  = 0;
	uint32_t bad_seq_  = 0;
    uint64_t discard_count_ = 0;
};

} // namespace cpp_streamer

#endif // RTP_SESSION_HPP
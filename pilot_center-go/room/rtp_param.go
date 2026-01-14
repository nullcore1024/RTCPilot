package room

// RtpParam 表示RTP参数
type RtpParam struct {
	AVType          string   `json:"av_type"`
	Codec           string   `json:"codec"`
	FmtpParam       string   `json:"fmtp_param"`
	RtcpFeatures    []string `json:"rtcp_features"`
	Channel         int      `json:"channel"`
	SSRC            uint32   `json:"ssrc"`
	PayloadType     int      `json:"payload_type"`
	ClockRate       int      `json:"clock_rate"`
	RtxSSRC         uint32   `json:"rtx_ssrc"`
	RtxPayloadType  int      `json:"rtx_payload_type"`
	UseNack         bool     `json:"use_nack"`
	KeyRequest      bool     `json:"key_request"`
	MidExtID        int      `json:"mid_ext_id"`
	TccExtID        int      `json:"tcc_ext_id"`
}

// FromDict 从字典创建RTP参数
func (r *RtpParam) FromDict(data map[string]interface{}) {
	if val, ok := data["av_type"].(string); ok {
		r.AVType = val
	}
	if val, ok := data["codec"].(string); ok {
		r.Codec = val
	}
	if val, ok := data["fmtp_param"].(string); ok {
		r.FmtpParam = val
	}
	if val, ok := data["rtcp_features"].([]interface{}); ok {
		for _, v := range val {
			if str, ok := v.(string); ok {
				r.RtcpFeatures = append(r.RtcpFeatures, str)
			}
		}
	}
	if val, ok := data["channel"].(float64); ok {
		r.Channel = int(val)
	}
	if val, ok := data["ssrc"].(float64); ok {
		r.SSRC = uint32(val)
	}
	if val, ok := data["payload_type"].(float64); ok {
		r.PayloadType = int(val)
	}
	if val, ok := data["clock_rate"].(float64); ok {
		r.ClockRate = int(val)
	}
	if val, ok := data["rtx_ssrc"].(float64); ok {
		r.RtxSSRC = uint32(val)
	}
	if val, ok := data["rtx_payload_type"].(float64); ok {
		r.RtxPayloadType = int(val)
	}
	if val, ok := data["use_nack"].(bool); ok {
		r.UseNack = val
	}
	if val, ok := data["key_request"].(bool); ok {
		r.KeyRequest = val
	}
	if val, ok := data["mid_ext_id"].(float64); ok {
		r.MidExtID = int(val)
	}
	if val, ok := data["tcc_ext_id"].(float64); ok {
		r.TccExtID = int(val)
	}
}

// ToDict 将RTP参数转换为字典
func (r *RtpParam) ToDict() map[string]interface{} {
	return map[string]interface{}{
		"av_type":          r.AVType,
		"codec":            r.Codec,
		"fmtp_param":       r.FmtpParam,
		"rtcp_features":    r.RtcpFeatures,
		"channel":          r.Channel,
		"ssrc":             r.SSRC,
		"payload_type":     r.PayloadType,
		"clock_rate":       r.ClockRate,
		"rtx_ssrc":         r.RtxSSRC,
		"rtx_payload_type": r.RtxPayloadType,
		"use_nack":         r.UseNack,
		"key_request":      r.KeyRequest,
		"mid_ext_id":       r.MidExtID,
		"tcc_ext_id":       r.TccExtID,
	}
}

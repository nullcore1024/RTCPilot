package room

// PushInfo 表示推流信息
type PushInfo struct {
	PusherID string    `json:"pusherId"`
	RtpParam *RtpParam `json:"rtpParam,omitempty"`
}

// SetRtpParam 设置RTP参数
func (p *PushInfo) SetRtpParam(rtpParam *RtpParam) {
	p.RtpParam = rtpParam
}

// ToDict 将推流信息转换为字典
func (p *PushInfo) ToDict() map[string]interface{} {
	result := map[string]interface{}{
		"pusherId": p.PusherID,
	}

	if p.RtpParam != nil {
		result["rtpParam"] = p.RtpParam.ToDict()
	}

	return result
}

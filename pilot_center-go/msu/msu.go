package msu

import (
	"time"
)

// Session 定义会话接口类型
type Session interface {}

// Msu 表示媒体服务器单元
type Msu struct {
	Session   Session
	MsuID     string
	AliveMs   int64
}

// NewMsu 创建一个新的MSU
func NewMsu(session Session, msuID string) *Msu {
	return &Msu{
		Session: session,
		MsuID:   msuID,
		AliveMs: time.Now().UnixNano() / int64(time.Millisecond),
	}
}

// Touch 更新MSU的活跃时间
func (m *Msu) Touch() {
	m.AliveMs = time.Now().UnixNano() / int64(time.Millisecond)
}

// IsAlive 检查MSU是否活跃
func (m *Msu) IsAlive(ttlMs int64, nowMs int64) bool {
	if nowMs == 0 {
		nowMs = time.Now().UnixNano() / int64(time.Millisecond)
	}
	return nowMs-m.AliveMs < ttlMs
}

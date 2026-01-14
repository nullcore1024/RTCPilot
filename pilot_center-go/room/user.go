package room

import (
	"sync"
)

// User 表示房间中的用户
type User struct {
	UserID     string
	Name       string
	sessions   map[string]interface{}
	pushers    map[string]*PushInfo
	sessionsMu sync.RWMutex
	pushersMu  sync.RWMutex
}

// NewUser 创建一个新的用户
func NewUser(userID, name string) *User {
	return &User{
		UserID:   userID,
		Name:     name,
		sessions: make(map[string]interface{}),
		pushers:  make(map[string]*PushInfo),
	}
}

// AddSession 添加会话到用户
func (u *User) AddSession(session interface{}) {
	u.sessionsMu.Lock()
	defer u.sessionsMu.Unlock()

	// 简化实现，只保留一个会话
	for key := range u.sessions {
		delete(u.sessions, key)
	}

	u.sessions["default"] = session
}

// RemoveSession 从用户移除会话
func (u *User) RemoveSession(session interface{}) {
	u.sessionsMu.Lock()
	defer u.sessionsMu.Unlock()

	for key, s := range u.sessions {
		if s == session {
			delete(u.sessions, key)
			break
		}
	}
}

// GetSession 获取用户的会话
func (u *User) GetSession() interface{} {
	u.sessionsMu.RLock()
	defer u.sessionsMu.RUnlock()

	for _, session := range u.sessions {
		return session
	}

	return nil
}

// HasSessions 检查用户是否有会话
func (u *User) HasSessions() bool {
	u.sessionsMu.RLock()
	defer u.sessionsMu.RUnlock()

	return len(u.sessions) > 0
}

// GetName 获取用户名称
func (u *User) GetName() string {
	return u.Name
}

// SetPusherInfo 设置用户的推流信息
func (u *User) SetPusherInfo(pushInfo *PushInfo) {
	u.pushersMu.Lock()
	defer u.pushersMu.Unlock()

	u.pushers[pushInfo.PusherID] = pushInfo
}

// GetPusherInfo 获取用户的推流信息
func (u *User) GetPusherInfo() map[string]*PushInfo {
	u.pushersMu.RLock()
	defer u.pushersMu.RUnlock()

	result := make(map[string]*PushInfo)
	for key, value := range u.pushers {
		result[key] = value
	}

	return result
}

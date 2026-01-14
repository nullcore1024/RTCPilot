package websocket_protoo

import (
	"github.com/gorilla/websocket"
	"pilot_center-go/logger"
	"sync"
)

// Session 定义WebSocket会话的接口
type Session interface {
	SendResponseOK(reqID interface{}, data interface{}) error
	SendResponseError(reqID interface{}, code int, reason string) error
	SendNotification(method string, data interface{}) error
	Close() error
}

// WsSession 实现WebSocket会话的接口
type WsSession struct {
	conn         *websocket.Conn
	peer         string
	logger       *logger.Logger
	closed       bool
	mu           sync.Mutex
}

// NewSession 创建一个新的WebSocket会话
func NewSession(conn *websocket.Conn, peer string) *WsSession {
	return &WsSession{
		conn:   conn,
		peer:   peer,
		logger: logger.GetLogger(),
		closed: false,
	}
}

// SendResponseOK 发送成功响应
func (s *WsSession) SendResponseOK(reqID interface{}, data interface{}) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}

	response := ResponseMessage{
		Response: true,
		ID:       reqID,
		OK:       true,
		Data:     data,
	}

	return s.conn.WriteJSON(response)
}

// SendResponseError 发送错误响应
func (s *WsSession) SendResponseError(reqID interface{}, code int, reason string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}

	response := ResponseMessage{
		Response:    true,
		ID:          reqID,
		OK:          false,
		ErrorCode:   code,
		ErrorReason: reason,
	}

	return s.conn.WriteJSON(response)
}

// SendNotification 发送通知
func (s *WsSession) SendNotification(method string, data interface{}) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}

	notification := NotificationMessage{
		Notification: true,
		Method:       method,
		Data:         data,
	}

	return s.conn.WriteJSON(notification)
}

// Close 关闭WebSocket会话
func (s *WsSession) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}

	s.closed = true
	return s.conn.Close()
}

// GetPeer 返回会话的远程地址
func (s *WsSession) GetPeer() string {
	return s.peer
}

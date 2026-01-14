package websocket_protoo

// RequestMessage 表示请求消息
type RequestMessage struct {
	Request bool        `json:"request"`
	ID      interface{} `json:"id"`
	Method  string      `json:"method"`
	Data    interface{} `json:"data"`
}

// ResponseMessage 表示响应消息
type ResponseMessage struct {
	Response    bool        `json:"response"`
	ID          interface{} `json:"id"`
	OK          bool        `json:"ok"`
	Data        interface{} `json:"data,omitempty"`
	ErrorCode   int         `json:"errorCode,omitempty"`
	ErrorReason string      `json:"errorReason,omitempty"`
}

// NotificationMessage 表示通知消息
type NotificationMessage struct {
	Notification bool        `json:"notification"`
	Method       string      `json:"method"`
	Data         interface{} `json:"data"`
}

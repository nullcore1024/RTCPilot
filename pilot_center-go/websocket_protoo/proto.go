package websocket_protoo

// RegisterRequest 注册MSU请求
type RegisterRequest struct {
	ID string `json:"id"`
}

// RegisterResponse 注册MSU响应
type RegisterResponse struct {
	Registered bool   `json:"registered"`
	MsuID      string `json:"msuId"`
}

// JoinRoomRequest 加入房间请求
type JoinRoomRequest struct {
	RoomID   string `json:"roomId"`
	UserID   string `json:"userId"`
	UserName string `json:"userName"`
	Audience bool   `json:"audience"`
}

// PushNotification 推流通知
type PushNotification struct {
	RoomID     string        `json:"roomId"`
	UserID     string        `json:"userId"`
	UserName   string        `json:"userName"`
	Publishers []interface{} `json:"publishers"`
}

// PullRemoteStreamNotification 拉取远程流通知
type PullRemoteStreamNotification struct {
	RoomID       string `json:"roomId"`
	UserID       string `json:"userId"`
	PusherUserID string `json:"pusher_user_id"`
}

// UserDisconnectNotification 用户断开连接通知
type UserDisconnectNotification struct {
	RoomID string `json:"roomId"`
	UserID string `json:"userId"`
}

// UserLeaveNotification 用户离开通知
type UserLeaveNotification struct {
	RoomID string `json:"roomId"`
	UserID string `json:"userId"`
}

// TextMessageNotification 文本消息通知
type TextMessageNotification struct {
	RoomID   string `json:"roomId"`
	UserID   string `json:"userId"`
	UserName string `json:"userName"`
	Message  string `json:"message"`
}

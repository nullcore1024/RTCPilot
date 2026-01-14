package room

import (
	"encoding/json"
	"pilot_center-go/logger"
	"sync"
)

// Room 表示WebSocket通信中的房间
type Room struct {
	RoomID   string
	logger   *logger.Logger
	users    map[string]*User
	sessions map[string]interface{}
	usersMu  sync.RWMutex
	sessionsMu sync.RWMutex
}

// NewRoom 创建一个新的房间
func NewRoom(roomID string) *Room {
	return &Room{
		RoomID:   roomID,
		logger:   logger.GetLogger(),
		users:    make(map[string]*User),
		sessions: make(map[string]interface{}),
	}
}

// AddUser 添加用户到房间
func (r *Room) AddUser(user *User) bool {
	r.usersMu.Lock()
	defer r.usersMu.Unlock()

	if _, exists := r.users[user.UserID]; exists {
		return false
	}

	r.users[user.UserID] = user
	return true
}

// RemoveUser 从房间移除用户
func (r *Room) RemoveUser(userID string) bool {
	r.usersMu.Lock()
	defer r.usersMu.Unlock()

	if _, exists := r.users[userID]; exists {
		delete(r.users, userID)
		return true
	}

	return false
}

// GetUser 获取房间中的用户
func (r *Room) GetUser(userID string) *User {
	r.usersMu.RLock()
	defer r.usersMu.RUnlock()

	return r.users[userID]
}

// ListUsers 列出房间中的所有用户
func (r *Room) ListUsers() []*User {
	r.usersMu.RLock()
	defer r.usersMu.RUnlock()

	result := make([]*User, 0, len(r.users))
	for _, user := range r.users {
		result = append(result, user)
	}

	return result
}

// UserCount 获取房间中的用户数量
func (r *Room) UserCount() int {
	r.usersMu.RLock()
	defer r.usersMu.RUnlock()

	return len(r.users)
}

// AddSession 添加会话到房间
func (r *Room) AddSession(peer string, session interface{}) {
	r.sessionsMu.Lock()
	defer r.sessionsMu.Unlock()

	r.sessions[peer] = session
}

// RemoveSession 从房间移除会话
func (r *Room) RemoveSession(peer string) {
	r.sessionsMu.Lock()
	defer r.sessionsMu.Unlock()

	delete(r.sessions, peer)
}

// HandleJoin 处理用户加入房间
func (r *Room) HandleJoin(userID, userName string, audience bool, session interface{}) interface{} {
	r.logger.Infof("用户 %s(%s) 加入房间 %s", userID, userName, r.RoomID)

	// 检查用户是否已存在
	existing := r.GetUser(userID)
	if existing == nil {
		// 创建新用户
		user := NewUser(userID, userName)
		r.AddUser(user)
		existing = user
	} else {
		r.logger.Infof("用户 %s 已存在于房间 %s", userID, r.RoomID)
	}

	// 关联会话
	if session != nil {
		existing.AddSession(session)
		// 注册会话到房间
		if getPeer, ok := session.(interface {
			GetPeer() string
		}); ok {
			peer := getPeer.GetPeer()
			r.AddSession(peer, session)
		}
	}

	// 通知其他用户新用户加入
	if !audience {
		r.NotifyNewUser(userID, userName, userID)
	}

	// 构建响应数据
	responseData := map[string]interface{}{
		"code":    0,
		"message": "join success",
		"roomId":  r.RoomID,
		"users":   []map[string]interface{}{},
	}

	// 添加其他用户信息
	for _, user := range r.ListUsers() {
		// 跳过当前用户
		if user.UserID == userID {
			continue
		}

		userInfo := map[string]interface{}{
			"userId":   user.UserID,
			"userName": user.Name,
			"pushers":  []map[string]interface{}{},
		}

		// 添加推流信息
		pushers := user.GetPusherInfo()
		for _, pusher := range pushers {
			userInfo["pushers"] = append(userInfo["pushers"].([]map[string]interface{}), pusher.ToDict())
		}

		responseData["users"] = append(responseData["users"].([]map[string]interface{}), userInfo)
	}

	return responseData
}

// BroadcastExceptUser 向房间中的所有用户广播通知，除了指定的用户
func (r *Room) BroadcastExceptUser(method string, payload map[string]interface{}, exceptUserID string) {
	// 获取要排除的会话
	var excludedSession interface{}
	if exceptUserID != "" {
		user := r.GetUser(exceptUserID)
		if user != nil {
			excludedSession = user.GetSession()
		}
	}

	// 广播通知
	r.sessionsMu.RLock()
	defer r.sessionsMu.RUnlock()

	for _, session := range r.sessions {
		// 跳过排除的会话
		if session == excludedSession {
			continue
		}

		// 发送通知
		if sendNotification, ok := session.(interface {
			SendNotification(method string, data map[string]interface{}) error
		}); ok {
			if err := sendNotification.SendNotification(method, payload); err != nil {
				r.logger.Errorf("发送通知失败: %v", err)
			}
		}
	}
}

// NotifyNewUser 通知房间中的其他用户有新用户加入
func (r *Room) NotifyNewUser(userID, userName, exceptUserID string) {
	payload := map[string]interface{}{
		"roomId":   r.RoomID,
		"userId":   userID,
		"userName": userName,
	}

	r.logger.Infof("通知房间 %s 成员新用户 %s(%s) 加入", r.RoomID, userID, userName)
	r.BroadcastExceptUser("newUser", payload, exceptUserID)
}

// HandleUserDisconnect 处理用户断开连接
func (r *Room) HandleUserDisconnectNotification(data map[string]interface{}, session interface{}) {
	userID, _ := data["userId"].(string)
	r.logger.Infof("处理用户 %s 在房间 %s 的断开连接通知", userID, r.RoomID)

	// 获取用户
	user := r.GetUser(userID)
	if user == nil {
		r.logger.Infof("用户 %s 在房间 %s 中不存在", userID, r.RoomID)
		return
	}

	// 通知其他用户
	payload := map[string]interface{}{
		"roomId": r.RoomID,
		"userId": userID,
	}
	r.BroadcastExceptUser("userDisconnect", payload, userID)

	// 移除会话关联
	user.RemoveSession(session)
}

// HandleUserLeave 处理用户离开房间
func (r *Room) HandleUserLeaveNotification(data map[string]interface{}, session interface{}) {
	userID, _ := data["userId"].(string)
	r.logger.Infof("处理用户 %s 在房间 %s 的离开通知", userID, r.RoomID)

	// 获取用户
	user := r.GetUser(userID)
	if user == nil {
		r.logger.Infof("用户 %s 在房间 %s 中不存在", userID, r.RoomID)
		return
	}

	// 通知其他用户
	payload := map[string]interface{}{
		"roomId": r.RoomID,
		"userId": userID,
	}
	r.BroadcastExceptUser("userLeave", payload, userID)

	// 移除会话关联
	user.RemoveSession(session)

	// 从房间移除用户
	r.RemoveUser(userID)
}

// HandleTextMessage 处理文本消息
func (r *Room) HandleTextMessageNotification(data map[string]interface{}, session interface{}) {
	userID, _ := data["userId"].(string)
	userName, _ := data["userName"].(string)
	message, _ := data["message"].(string)

	r.logger.Infof("处理用户 %s(%s) 在房间 %s 的文本消息: %s", userID, userName, r.RoomID, message)

	// 广播文本消息
	r.BroadcastExceptUser("textMessage", map[string]interface{}{
		"roomId":   r.RoomID,
		"userId":   userID,
		"userName": userName,
		"message":  message,
	}, userID)
}

// HandlePushNotification 处理推流通知
func (r *Room) HandlePushNotification(data map[string]interface{}, session interface{}) {
	userID, _ := data["userId"].(string)
	userName, _ := data["userName"].(string)
	publishers, _ := data["publishers"].([]interface{})

	r.logger.Infof("在房间 %s 中收到用户 %s(%s) 的推流通知，发布者: %v", r.RoomID, userID, userName, publishers)

	// 获取用户
	user := r.GetUser(userID)
	if user == nil {
		r.logger.Infof("在房间 %s 中不存在用户 %s", r.RoomID, userID)
		// 创建新用户
		user = NewUser(userID, userName)
		r.AddUser(user)
	} else {
		// 更新用户名
		if userName != "" {
			user.Name = userName
		}
	}

	// 处理发布者信息
	var pushInfoList []*PushInfo
	for _, pub := range publishers {
		if pubDict, ok := pub.(map[string]interface{}); ok {
			pusherID, _ := pubDict["pusherId"].(string)
			if pusherID == "" {
				continue
			}

			// 创建推流信息
			pushInfo := &PushInfo{PusherID: pusherID}

			// 解析RTP参数
			if rtpParamDict, ok := pubDict["rtpParam"].(map[string]interface{}); ok {
				rtpParam := &RtpParam{}
				rtpParam.FromDict(rtpParamDict)
				pushInfo.SetRtpParam(rtpParam)
			}

			// 添加到推流列表
			pushInfoList = append(pushInfoList, pushInfo)
			// 更新用户的推流信息
			user.SetPusherInfo(pushInfo)
		}
	}

	// 通知其他用户新的推流
	r.NotifyNewPushers(userID, user.Name, pushInfoList)
}

// NotifyNewPushers 通知房间中的其他用户有新的推流
func (r *Room) NotifyNewPushers(userID, userName string, pushInfoList []*PushInfo) {
	// 构建推流信息列表
	pushersList := []map[string]interface{}{}
	for _, pushInfo := range pushInfoList {
		pushersList = append(pushersList, pushInfo.ToDict())
	}

	// 广播通知
	payload := map[string]interface{}{
		"roomId":   r.RoomID,
		"userId":   userID,
		"userName": userName,
		"pushers":  pushersList,
	}

	r.BroadcastExceptUser("newPusher", payload, userID)
}

// HandlePullRemoteStream 处理拉取远程流通知
func (r *Room) HandlePullRemoteStreamNotification(data map[string]interface{}, session interface{}) {
	pusherUserID, _ := data["pusher_user_id"].(string)

	// 发送拉取请求给推流用户
	pusherUser := r.GetUser(pusherUserID)
	if pusherUser == nil {
		r.logger.Infof("在房间 %s 中不存在推流用户 %s", r.RoomID, pusherUserID)
		return
	}

	pusherSession := pusherUser.GetSession()
	if pusherSession == nil {
		r.logger.Infof("推流用户 %s 在房间 %s 中没有会话", pusherUserID, r.RoomID)
		return
	}

	// 记录数据
	dataBytes, _ := json.Marshal(data)
	r.logger.Infof("向房间 %s 中的推流用户 %s 发送拉取远程流通知: %s", r.RoomID, pusherUserID, string(dataBytes))

	// 发送通知
	if sendNotification, ok := pusherSession.(interface {
		SendNotification(method string, data map[string]interface{}) error
	}); ok {
		if err := sendNotification.SendNotification("pullRemoteStream", data); err != nil {
			r.logger.Errorf("发送拉取远程流通知失败: %v", err)
		}
	}
}

package room

import (
	"pilot_center-go/logger"
	"sync"
)

// RoomManager 管理多个房间实例
type RoomManager struct {
	rooms map[string]*Room
	mu    sync.RWMutex
	logger *logger.Logger
}

// NewRoomManager 创建一个新的房间管理器
func NewRoomManager() *RoomManager {
	return &RoomManager{
		rooms:  make(map[string]*Room),
		logger: logger.GetLogger(),
	}
}

// GetOrCreateRoom 获取或创建房间
func (rm *RoomManager) GetOrCreateRoom(roomID string) *Room {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	room := rm.rooms[roomID]
	if room == nil {
		room = NewRoom(roomID)
		rm.rooms[roomID] = room
	}

	return room
}

// DeleteRoom 删除房间
func (rm *RoomManager) DeleteRoom(roomID string) bool {
	rm.mu.Lock()
	defer rm.mu.Unlock()

	if _, exists := rm.rooms[roomID]; exists {
		delete(rm.rooms, roomID)
		return true
	}

	return false
}

// ListRooms 列出所有房间
func (rm *RoomManager) ListRooms() []*Room {
	rm.mu.RLock()
	defer rm.mu.RUnlock()

	result := make([]*Room, 0, len(rm.rooms))
	for _, room := range rm.rooms {
		result = append(result, room)
	}

	return result
}

// HandleJoin 处理用户加入房间
func (rm *RoomManager) HandleJoin(roomID, userID, userName string, audience bool, session interface{}) interface{} {
	room := rm.GetOrCreateRoom(roomID)
	return room.HandleJoin(userID, userName, audience, session)
}

// HandlePushNotification 处理推流通知
func (rm *RoomManager) HandlePushNotification(data interface{}, session interface{}) {
	if dataMap, ok := data.(map[string]interface{}); ok {
		if roomID, ok := dataMap["roomId"].(string); ok && roomID != "" {
			room := rm.GetOrCreateRoom(roomID)
			room.HandlePushNotification(dataMap, session)
		}
	}
}

// HandlePullRemoteStreamNotification 处理拉取远程流通知
func (rm *RoomManager) HandlePullRemoteStreamNotification(data interface{}, session interface{}) {
	if dataMap, ok := data.(map[string]interface{}); ok {
		if roomID, ok := dataMap["roomId"].(string); ok && roomID != "" {
			room := rm.GetOrCreateRoom(roomID)
			room.HandlePullRemoteStreamNotification(dataMap, session)
		}
	}
}

// HandleUserDisconnectNotification 处理用户断开连接通知
func (rm *RoomManager) HandleUserDisconnectNotification(data interface{}, session interface{}) {
	if dataMap, ok := data.(map[string]interface{}); ok {
		if roomID, ok := dataMap["roomId"].(string); ok && roomID != "" {
			room := rm.GetOrCreateRoom(roomID)
			room.HandleUserDisconnectNotification(dataMap, session)
		}
	}
}

// HandleUserLeaveNotification 处理用户离开通知
func (rm *RoomManager) HandleUserLeaveNotification(data interface{}, session interface{}) {
	if dataMap, ok := data.(map[string]interface{}); ok {
		if roomID, ok := dataMap["roomId"].(string); ok && roomID != "" {
			room := rm.GetOrCreateRoom(roomID)
			room.HandleUserLeaveNotification(dataMap, session)
		}
	}
}

// HandleTextMessageNotification 处理文本消息通知
func (rm *RoomManager) HandleTextMessageNotification(data interface{}, session interface{}) {
	if dataMap, ok := data.(map[string]interface{}); ok {
		if roomID, ok := dataMap["roomId"].(string); ok && roomID != "" {
			room := rm.GetOrCreateRoom(roomID)
			room.HandleTextMessageNotification(dataMap, session)
		}
	}
}

// GetUser 获取房间中的用户
func (rm *RoomManager) GetUser(roomID, userID string) *User {
	rm.mu.RLock()
	defer rm.mu.RUnlock()

	if room, exists := rm.rooms[roomID]; exists {
		return room.GetUser(userID)
	}

	return nil
}

// ListUsers 列出房间中的所有用户
func (rm *RoomManager) ListUsers(roomID string) []*User {
	rm.mu.RLock()
	defer rm.mu.RUnlock()

	if room, exists := rm.rooms[roomID]; exists {
		return room.ListUsers()
	}

	return []*User{}
}

// RoomUserCount 获取房间中的用户数量
func (rm *RoomManager) RoomUserCount(roomID string) int {
	rm.mu.RLock()
	defer rm.mu.RUnlock()

	if room, exists := rm.rooms[roomID]; exists {
		return room.UserCount()
	}

	return 0
}

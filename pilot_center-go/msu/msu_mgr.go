package msu

import (
	"pilot_center-go/logger"
	"sync"
	"time"
)

// MsuManager 管理多个MSU实例
type MsuManager struct {
	logger *logger.Logger
	items  map[string]*Msu
	rooms  map[string]*Msu
	mu     sync.RWMutex
}

// NewMsuManager 创建一个新的MSU管理器
func NewMsuManager() *MsuManager {
	return &MsuManager{
		logger: logger.GetLogger(),
		items:  make(map[string]*Msu),
		rooms:  make(map[string]*Msu),
	}
}

// AddOrUpdate 添加或更新MSU
func (mm *MsuManager) AddOrUpdate(session interface{}, msuID string, aliveMs ...int64) interface{} {
	if msuID == "" {
		return nil
	}

	mm.mu.Lock()
	defer mm.mu.Unlock()

	item := mm.items[msuID]
	if item == nil {
		// 创建新MSU
		item = NewMsu(session, msuID)
		mm.items[msuID] = item
		mm.logger.Infof("MSU创建: %s", msuID)
	} else {
		// 更新会话引用
		item.Session = session
	}

	// 更新活跃时间
	if len(aliveMs) > 0 && aliveMs[0] > 0 {
		item.AliveMs = aliveMs[0]
	} else {
		item.Touch()
	}

	return item
}

// Get 根据MSU ID获取MSU
func (mm *MsuManager) Get(msuID string) interface{} {
	mm.mu.RLock()
	defer mm.mu.RUnlock()

	return mm.items[msuID]
}

// Remove 根据MSU ID移除MSU
func (mm *MsuManager) Remove(msuID string) bool {
	mm.mu.Lock()
	defer mm.mu.Unlock()

	if _, exists := mm.items[msuID]; exists {
		delete(mm.items, msuID)
		mm.logger.Infof("MSU移除: %s", msuID)
		return true
	}

	return false
}

// Touch 更新MSU的活跃时间
func (mm *MsuManager) Touch(msuID string) {
	mm.mu.Lock()
	defer mm.mu.Unlock()

	if item := mm.items[msuID]; item != nil {
		item.Touch()
	}
}

// HandleJoinRoom 处理MSU加入房间
func (mm *MsuManager) HandleJoinRoom(roomID, userID, userName string) error {
	mm.logger.Infof("处理MSU加入房间: room_id=%s, user_id=%s, user_name=%s", roomID, userID, userName)

	mm.mu.RLock()
	// 根据房间ID获取MSU
	msu := mm.rooms[roomID]
	if msu == nil {
		// 如果房间没有关联的MSU，选择第一个可用的MSU
		for _, item := range mm.items {
			msu = item
			break
		}
		// 如果找到了MSU，关联到房间
		if msu != nil {
			mm.mu.RUnlock()
			mm.mu.Lock()
			mm.rooms[roomID] = msu
			mm.mu.Unlock()
			mm.mu.RLock()
		}
	}
	mm.mu.RUnlock()

	if msu == nil {
		mm.logger.Warningf("没有可用的MSU来加入房间: %s", roomID)
		return nil
	}

	// 发送通知给MSU
	if msu.Session != nil {
		// 使用反射来调用SendNotification方法，避免导入循环
		if sendNotification, ok := msu.Session.(interface {
			SendNotification(method string, data map[string]interface{}) error
		}); ok {
			return sendNotification.SendNotification("joinRoom", map[string]interface{}{
				"roomId":   roomID,
				"userId":   userID,
				"userName": userName,
			})
		}
	}

	return nil
}

// GetMsuByRoomID 根据房间ID获取MSU
func (mm *MsuManager) GetMsuByRoomID(roomID string) *Msu {
	mm.mu.RLock()
	defer mm.mu.RUnlock()

	// 先从房间映射中查找
	msu := mm.rooms[roomID]
	if msu != nil {
		return msu
	}

	// 如果没找到，选择第一个可用的MSU
	for _, item := range mm.items {
		return item
	}

	return nil
}

// ListIDs 列出所有MSU ID
func (mm *MsuManager) ListIDs() []string {
	mm.mu.RLock()
	defer mm.mu.RUnlock()

	result := make([]string, 0, len(mm.items))
	for id := range mm.items {
		result = append(result, id)
	}

	return result
}

// PruneStale 清理过期的MSU
func (mm *MsuManager) PruneStale(ttlMs int64, nowMs ...int64) []string {
	var now int64
	if len(nowMs) > 0 {
		now = nowMs[0]
	} else {
		now = time.Now().UnixNano() / int64(time.Millisecond)
	}

	mm.mu.Lock()
	defer mm.mu.Unlock()

	removed := []string{}
	for id, item := range mm.items {
		if !item.IsAlive(ttlMs, now) {
			delete(mm.items, id)
			removed = append(removed, id)
		}
	}

	if len(removed) > 0 {
		mm.logger.Infof("清理过期MSU: %v", removed)
	}

	return removed
}

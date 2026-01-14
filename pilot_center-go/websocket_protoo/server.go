package websocket_protoo

import (
	"encoding/json"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"pilot_center-go/logger"
)

// ServerOptions 表示WebSocket服务器的配置选项
type ServerOptions struct {
	Host           string
	Port           int
	CertPath       string
	KeyPath        string
	Subpath        string
	ReadBufferSize int
	WriteBufferSize int
	ReadTimeout    int
	WriteTimeout   int
}

// RoomManager 定义房间管理器的接口
type RoomManager interface {
	HandleJoin(roomID, userID, userName string, audience bool, session interface{}) interface{}
	HandlePushNotification(data interface{}, session interface{})
	HandlePullRemoteStreamNotification(data interface{}, session interface{})
	HandleUserDisconnectNotification(data interface{}, session interface{})
	HandleUserLeaveNotification(data interface{}, session interface{})
	HandleTextMessageNotification(data interface{}, session interface{})
}

// MsuManager 定义MSU管理器的接口
type MsuManager interface {
	Get(msuID string) interface{}
	AddOrUpdate(session interface{}, msuID string, aliveMs ...int64) interface{}
	HandleJoinRoom(roomID, userID, userName string) error
}

// Server 实现WebSocket服务器
type Server struct {
	opts         ServerOptions
	roomManager  RoomManager
	msuManager   MsuManager
	logger       *logger.Logger
	server       *http.Server
	sessions     map[string]*WsSession
	sessionsMu   sync.RWMutex
	upgrader     websocket.Upgrader
}

// NewServer 创建一个新的WebSocket服务器
func NewServer(opts ServerOptions, roomManager RoomManager, msuManager MsuManager) *Server {
	return &Server{
		opts:        opts,
		roomManager: roomManager,
		msuManager:  msuManager,
		logger:      logger.GetLogger(),
		sessions:    make(map[string]*WsSession),
		upgrader: websocket.Upgrader{
			ReadBufferSize:  opts.ReadBufferSize,
			WriteBufferSize: opts.WriteBufferSize,
			CheckOrigin: func(r *http.Request) bool {
				return true // 允许所有来源的请求
			},
		},
	}
}

// Start 启动WebSocket服务器
func (s *Server) Start() error {
	// 设置HTTP处理函数
	http.HandleFunc(s.opts.Subpath, s.handleConnection)

	// 构建服务器地址
	addr := fmt.Sprintf("%s:%d", s.opts.Host, s.opts.Port)

	// 创建服务器配置
	s.server = &http.Server{
		Addr:         addr,
		ReadTimeout:  time.Duration(s.opts.ReadTimeout) * time.Second,
		WriteTimeout: time.Duration(s.opts.WriteTimeout) * time.Second,
	}

	// 启动服务器
	s.logger.Infof("准备启动WebSocket服务器，监听 %s%s (TLS: %v)", addr, s.opts.Subpath, 0 != len(s.opts.CertPath) && 0 != len(s.opts.KeyPath))
	s.logger.Infof("服务器配置: Host=%s, Port=%d, Subpath=%s", s.opts.Host, s.opts.Port, s.opts.Subpath)

	var err error
	if 0 != len(s.opts.CertPath) && 0 != len(s.opts.KeyPath) {
		// 使用TLS
		err = s.server.ListenAndServeTLS(s.opts.CertPath, s.opts.KeyPath)
	} else {
		// 不使用TLS
		err = s.server.ListenAndServe()
	}

	if err != nil {
		s.logger.Errorf("启动WebSocket服务器失败: %v", err)
	}

	return err
}

// Stop 停止WebSocket服务器
func (s *Server) Stop() error {
	if s.server == nil {
		return nil
	}

	// 关闭所有会话
	s.sessionsMu.RLock()
	for _, session := range s.sessions {
		session.Close()
	}
	s.sessionsMu.RUnlock()

	// 停止服务器
	s.logger.Info("WebSocket服务器正在停止")
	return s.server.Close()
}

// handleConnection 处理新的WebSocket连接
func (s *Server) handleConnection(w http.ResponseWriter, r *http.Request) {
	// 检查路径
	if r.URL.Path != s.opts.Subpath {
		s.logger.Warningf("拒绝连接: 路径不匹配 %s (期望 %s)", r.URL.Path, s.opts.Subpath)
		http.Error(w, "路径不匹配", http.StatusForbidden)
		return
	}

	// 升级HTTP连接为WebSocket连接
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		s.logger.Errorf("升级WebSocket连接失败: %v", err)
		return
	}

	// 获取客户端地址
	peer := conn.RemoteAddr().String()

	// 创建新的会话
	session := NewSession(conn, peer)

	// 注册会话
	s.sessionsMu.Lock()
	s.sessions[peer] = session
	s.sessionsMu.Unlock()

	// 开始处理消息
	go s.handleMessages(session, peer)
}

// handleMessages 处理WebSocket会话的消息
func (s *Server) handleMessages(session *WsSession, peer string) {
	defer func() {
		// 关闭连接
		session.Close()

		// 注销会话
		s.sessionsMu.Lock()
		delete(s.sessions, peer)
		s.sessionsMu.Unlock()

		s.logger.Infof("客户端断开连接: %s", peer)
	}()

	for {
		// 读取消息
		var (
			message []byte
			err error 
		)
		if _, message, err = session.conn.ReadMessage(); err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				s.logger.Errorf("读取WebSocket消息失败: %v", err)
			}
			break
		}

		// 处理消息
		if err = s.handleMessage(session, message); err != nil {
			s.logger.Errorf("处理WebSocket消息失败: %v", err)
			continue
		}
	}
}

// handleMessage 处理不同类型的WebSocket消息
func (s *Server) handleMessage(session *WsSession, message []byte) error {
	var req RequestMessage
	json.Unmarshal([]byte(message), &req)
	// 处理请求消息
	if req.Request {
		return s.handleRequest(session, req)
	}

	var resp ResponseMessage
	json.Unmarshal([]byte(message), &resp)
	if resp.Response {
		return s.handleResponse(session, resp)
	}

	var notify NotificationMessage
	json.Unmarshal([]byte(message), &notify)
	// 处理通知消息
	if notify.Notification {
		return s.handleNotification(session, notify)
	}

	s.logger.Warningf("未知的消息类型: %v", message)
	return nil
}

// handleRequest 处理请求消息
func (s *Server) handleRequest(session *WsSession, req RequestMessage) error {
	reqID := req.ID
	method := req.Method

	// 处理不同的方法
	switch method {
	case "echo":
		// 回显数据
		data, _ := req.Data.(map[string]interface{})
		return session.SendResponseOK(reqID, map[string]interface{}{"echo": data})

	case "register":
		// 注册MSU
		dataMap, ok := req.Data.(map[string]interface{})
		if !ok {
			return session.SendResponseError(reqID, 400, "无效的数据格式")
		}
		
		// 解析注册请求
		registerReq := RegisterRequest{}
		if id, ok := dataMap["id"].(string); ok {
			registerReq.ID = id
		}
		
		if 0 == len(registerReq.ID) {
			return session.SendResponseError(reqID, 400, "无效的MSU ID")
		}

		// 更新MSU管理器
		s.msuManager.AddOrUpdate(session, registerReq.ID)

		response := RegisterResponse{
			Registered: true,
			MsuID:      registerReq.ID,
		}
		return session.SendResponseOK(reqID, response)

	case "join":
		// 加入房间
		dataMap, ok := req.Data.(map[string]interface{})
		if !ok {
			return session.SendResponseError(reqID, 400, "无效的数据格式")
		}
		
		// 解析加入房间请求
		joinReq := JoinRoomRequest{}
		if roomId, ok := dataMap["roomId"].(string); ok {
			joinReq.RoomID = roomId
		}
		if userId, ok := dataMap["userId"].(string); ok {
			joinReq.UserID = userId
		}
		if userName, ok := dataMap["userName"].(string); ok {
			joinReq.UserName = userName
		}
		if audience, ok := dataMap["audience"].(bool); ok {
			joinReq.Audience = audience
		}

		if 0 == len(joinReq.RoomID) || 0 == len(joinReq.UserID) {
			return session.SendResponseError(reqID, 400, "无效的房间ID或用户ID")
		}

		// 调用房间管理器处理加入请求
		result := s.roomManager.HandleJoin(joinReq.RoomID, joinReq.UserID, joinReq.UserName, joinReq.Audience, session)

		// 如果不是观众，通知MSU管理器
		if !joinReq.Audience {
			if err := s.msuManager.HandleJoinRoom(joinReq.RoomID, joinReq.UserID, joinReq.UserName); err != nil {
				s.logger.Errorf("MSU加入房间失败: %v", err)
			}
		}

		return session.SendResponseOK(reqID, result)

	case "push":
		// 处理推送请求
		dataMap, ok := req.Data.(map[string]interface{})
		if !ok {
			return session.SendResponseError(reqID, 400, "无效的数据格式")
		}
		
		// 解析推送请求
		roomId, _ := dataMap["roomId"].(string)
		userId, _ := dataMap["userId"].(string)
		streamId, _ := dataMap["streamId"].(string)
		streamType, _ := dataMap["type"].(string)

		// 调用房间管理器处理推送通知
		pushNotify := PushNotification{
			RoomID:   roomId,
			UserID:   userId,
			UserName: "", // 从会话或其他地方获取
			Publishers: []interface{}{map[string]interface{}{
				"userId":   userId,
				"streamId": streamId,
				"type":     streamType,
			}},
		}
		s.roomManager.HandlePushNotification(pushNotify, session)

		// 返回成功响应
		return session.SendResponseOK(reqID, map[string]interface{}{
			"code":    0,
			"message": "push success",
			"roomId":  roomId,
		})

	case "pull":
		// 处理拉取请求
		dataMap, ok := req.Data.(map[string]interface{})
		if !ok {
			return session.SendResponseError(reqID, 400, "无效的数据格式")
		}
		
		// 解析拉取请求
		roomId, _ := dataMap["roomId"].(string)
		userId, _ := dataMap["userId"].(string)
		source, _ := dataMap["source"].(string)

		// 调用房间管理器处理拉取远程流通知
		pullNotify := PullRemoteStreamNotification{
			RoomID:       roomId,
			UserID:       userId,
			PusherUserID: source,
		}
		s.roomManager.HandlePullRemoteStreamNotification(pullNotify, session)

		// 返回成功响应
		return session.SendResponseOK(reqID, map[string]interface{}{
			"code":    0,
			"message": "pull success",
			"roomId":  roomId,
		})

	case "leave":
		// 处理离开房间请求
		dataMap, ok := req.Data.(map[string]interface{})
		if !ok {
			return session.SendResponseError(reqID, 400, "无效的数据格式")
		}
		
		// 解析离开房间请求
		roomId, _ := dataMap["roomId"].(string)
		userId, _ := dataMap["userId"].(string)

		// 调用房间管理器处理用户离开通知
		leaveNotify := UserLeaveNotification{
			RoomID: roomId,
			UserID: userId,
		}
		s.roomManager.HandleUserLeaveNotification(leaveNotify, session)

		// 返回成功响应
		return session.SendResponseOK(reqID, map[string]interface{}{
			"code":    0,
			"message": "leave success",
			"roomId":  roomId,
		})

	default:
		return session.SendResponseError(reqID, 404, fmt.Sprintf("未知的方法: %s", method))
	}
}

// handleResponse 处理响应消息
func (s *Server) handleResponse(session *WsSession, resp ResponseMessage) error {
	// 目前不需要处理客户端的响应
	return nil
}

// handleNotification 处理通知消息
func (s *Server) handleNotification(session *WsSession, notify NotificationMessage) error {
	method := notify.Method

	// 处理不同的通知方法
	switch method {
	case "push":
		// 处理推送通知
		dataMap, ok := notify.Data.(map[string]interface{})
		if ok {
			pushNotify := PushNotification{}
			if roomId, ok := dataMap["roomId"].(string); ok {
				pushNotify.RoomID = roomId
			}
			if userId, ok := dataMap["userId"].(string); ok {
				pushNotify.UserID = userId
			}
			if userName, ok := dataMap["userName"].(string); ok {
				pushNotify.UserName = userName
			}
			if publishers, ok := dataMap["publishers"].([]interface{}); ok {
				pushNotify.Publishers = publishers
			}
			s.roomManager.HandlePushNotification(pushNotify, session)
		}

	case "pullRemoteStream":
		// 处理拉取远程流通知
		dataMap, ok := notify.Data.(map[string]interface{})
		if ok {
			pullNotify := PullRemoteStreamNotification{}
			if roomId, ok := dataMap["roomId"].(string); ok {
				pullNotify.RoomID = roomId
			}
			if userId, ok := dataMap["userId"].(string); ok {
				pullNotify.UserID = userId
			}
			if pusherUserId, ok := dataMap["pusher_user_id"].(string); ok {
				pullNotify.PusherUserID = pusherUserId
			}
			s.roomManager.HandlePullRemoteStreamNotification(pullNotify, session)
		}

	case "userDisconnect":
		// 处理用户断开连接通知
		dataMap, ok := notify.Data.(map[string]interface{})
		if ok {
			disconnectNotify := UserDisconnectNotification{}
			if roomId, ok := dataMap["roomId"].(string); ok {
				disconnectNotify.RoomID = roomId
			}
			if userId, ok := dataMap["userId"].(string); ok {
				disconnectNotify.UserID = userId
			}
			s.roomManager.HandleUserDisconnectNotification(disconnectNotify, session)
		}

	case "userLeave":
		// 处理用户离开通知
		dataMap, ok := notify.Data.(map[string]interface{})
		if ok {
			leaveNotify := UserLeaveNotification{}
			if roomId, ok := dataMap["roomId"].(string); ok {
				leaveNotify.RoomID = roomId
			}
			if userId, ok := dataMap["userId"].(string); ok {
				leaveNotify.UserID = userId
			}
			s.roomManager.HandleUserLeaveNotification(leaveNotify, session)
		}

	case "textMessage":
		// 处理文本消息通知
		dataMap, ok := notify.Data.(map[string]interface{})
		if ok {
			textNotify := TextMessageNotification{}
			if roomId, ok := dataMap["roomId"].(string); ok {
				textNotify.RoomID = roomId
			}
			if userId, ok := dataMap["userId"].(string); ok {
				textNotify.UserID = userId
			}
			if userName, ok := dataMap["userName"].(string); ok {
				textNotify.UserName = userName
			}
			if message, ok := dataMap["message"].(string); ok {
				textNotify.Message = message
			}
			s.roomManager.HandleTextMessageNotification(textNotify, session)
		}

	default:
		s.logger.Warningf("未知的通知方法: %s", method)
	}

	return nil
}

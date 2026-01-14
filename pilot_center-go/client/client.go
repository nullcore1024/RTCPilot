package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/url"
	"time"

	"github.com/gorilla/websocket"
)

// RequestMessage 表示请求消息
type RequestMessage struct {
	Request bool                   `json:"request"`
	ID      interface{}            `json:"id"`
	Method  string                 `json:"method"`
	Data    map[string]interface{} `json:"data"`
}

// ResponseMessage 表示响应消息
type ResponseMessage struct {
	Response    bool                   `json:"response"`
	ID          interface{}            `json:"id"`
	OK          bool                   `json:"ok"`
	Data        map[string]interface{} `json:"data,omitempty"`
	ErrorCode   int                    `json:"errorCode,omitempty"`
	ErrorReason string                 `json:"errorReason,omitempty"`
}

// NotificationMessage 表示通知消息
type NotificationMessage struct {
	Notification bool                   `json:"notification"`
	Method       string                 `json:"method"`
	Data         map[string]interface{} `json:"data"`
}

func main() {
	// WebSocket服务器地址
	u := url.URL{Scheme: "ws", Host: "localhost:4443", Path: "/pilot/center"}
	log.Printf("正在连接到 %s", u.String())

	// 连接到WebSocket服务器
	c, _, err := websocket.DefaultDialer.Dial(u.String(), nil)
	if err != nil {
		log.Fatalf("拨号失败: %v", err)
	}
	defer c.Close()

	// 创建一个通道用于接收消息
	messageChan := make(chan interface{})
	errChan := make(chan error)

	// 启动一个goroutine来接收消息
	go func() {
		for {
			_, message, err := c.ReadMessage()
			if err != nil {
				errChan <- err
				return
			}

			// 解析消息
			var req RequestMessage
			if err := json.Unmarshal(message, &req); err == nil && req.Request {
				messageChan <- req
				continue
			}

			var resp ResponseMessage
			if err := json.Unmarshal(message, &resp); err == nil && resp.Response {
				messageChan <- resp
				continue
			}

			var notify NotificationMessage
			if err := json.Unmarshal(message, &notify); err == nil && notify.Notification {
				messageChan <- notify
				continue
			}

			log.Printf("接收到未知消息类型: %s", message)
		}
	}()

	// 测试join请求
	log.Println("测试join请求...")
	joinRequest := RequestMessage{
		Request: true,
		ID:      9311734,
		Method:  "join",
		Data: map[string]interface{}{
			"roomId":   "6scujmas",
			"userId":   "4443",
			"userName": "User_4443",
		},
	}

	if err := c.WriteJSON(joinRequest); err != nil {
		log.Fatalf("发送join请求失败: %v", err)
	}

	// 等待响应和通知
	for {
		select {
		case msg := <-messageChan:
			switch m := msg.(type) {
			case ResponseMessage:
				log.Printf("接收到响应: ID=%v, OK=%v", m.ID, m.OK)
				if m.Data != nil {
					jsonData, _ := json.MarshalIndent(m.Data, "", "  ")
					log.Printf("响应数据: %s", jsonData)
				}
				if m.ErrorCode != 0 {
					log.Printf("错误: %d - %s", m.ErrorCode, m.ErrorReason)
				}

			case NotificationMessage:
				log.Printf("接收到通知: Method=%s", m.Method)
				if m.Data != nil {
					jsonData, _ := json.MarshalIndent(m.Data, "", "  ")
					log.Printf("通知数据: %s", jsonData)
				}

			case RequestMessage:
				log.Printf("接收到请求: ID=%v, Method=%s", m.ID, m.Method)
				if m.Data != nil {
					jsonData, _ := json.MarshalIndent(m.Data, "", "  ")
					log.Printf("请求数据: %s", jsonData)
				}

			default:
				log.Printf("接收到未知消息: %T", m)
			}

		case err := <-errChan:
			log.Fatalf("接收消息失败: %v", err)

		case <-time.After(30 * time.Second):
			log.Println("测试超时")
			return
		}
	}
}

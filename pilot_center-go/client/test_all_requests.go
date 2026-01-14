package main

import (
	"fmt"
	"net/url"
	"time"

	"github.com/gorilla/websocket"
)

func main() {
	// WebSocket服务器地址
	u := url.URL{Scheme: "ws", Host: "localhost:8080", Path: "/pilot/center"}
	fmt.Printf("正在尝试连接到: %s\n", u.String())

	// 配置WebSocket拨号器
	dialer := &websocket.Dialer{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		HandshakeTimeout: 10 * time.Second,
	}

	// 尝试连接
	c, _, err := dialer.Dial(u.String(), nil)
	if err != nil {
		fmt.Printf("连接失败: %v\n", err)
		return
	}
	defer c.Close()

	fmt.Println("连接成功!")

	// 测试请求列表
	tests := []struct {
		name     string
		request  map[string]interface{}
		expected string
	}{
		{
			name: "Join Room",
			request: map[string]interface{}{
				"request": true,
				"id":      1001,
				"method":  "join",
				"data": map[string]interface{}{
					"roomId":   "testroom",
					"userId":   "testuser",
					"userName": "Test User",
				},
			},
			expected: "join success",
		},
		{
			name: "Register MSU",
			request: map[string]interface{}{
				"request": true,
				"id":      1002,
				"method":  "register",
				"data": map[string]interface{}{
					"id": "msu-001",
				},
			},
			expected: "register success",
		},
		{
			name: "Push Stream",
			request: map[string]interface{}{
				"request": true,
				"id":      1003,
				"method":  "push",
				"data": map[string]interface{}{
					"roomId":   "testroom",
					"userId":   "testuser",
					"streamId": "stream-001",
					"type":     "screen",
				},
			},
			expected: "push success",
		},
		{
			name: "Pull Stream",
			request: map[string]interface{}{
				"request": true,
				"id":      1004,
				"method":  "pull",
				"data": map[string]interface{}{
					"roomId":   "testroom",
					"userId":   "testuser",
					"streamId": "stream-001",
					"source":   "testuser",
				},
			},
			expected: "pull success",
		},
		{
			name: "Leave Room",
			request: map[string]interface{}{
				"request": true,
				"id":      1005,
				"method":  "leave",
				"data": map[string]interface{}{
					"roomId": "testroom",
					"userId": "testuser",
				},
			},
			expected: "leave success",
		},
	}

	// 运行所有测试
	for _, test := range tests {
		fmt.Printf("\n=== 测试: %s ===\n", test.name)
		
		// 发送请求
		err = c.WriteJSON(test.request)
		if err != nil {
			fmt.Printf("发送请求失败: %v\n", err)
			continue
		}
		fmt.Println("发送请求成功")

		// 等待响应
		fmt.Println("等待响应...")
		_, message, err := c.ReadMessage()
		if err != nil {
			fmt.Printf("读取响应失败: %v\n", err)
			continue
		}

		fmt.Printf("接收到响应: %s\n", message)
	}

	fmt.Println("\n=== 所有测试完成 ===")
}

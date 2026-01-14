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

	// 1. 测试join请求
	fmt.Println("\n=== 测试1: join请求 ===")
	joinRequest := map[string]interface{}{
		"request": true,
		"id":      1001,
		"method":  "join",
		"data": map[string]interface{}{
			"roomId":   "testroom",
			"userId":   "testuser",
			"userName": "Test User",
		},
	}
	
	err = c.WriteJSON(joinRequest)
	if err != nil {
		fmt.Printf("发送join请求失败: %v\n", err)
		return
	}
	fmt.Println("发送join请求成功")

	_, message, err := c.ReadMessage()
	if err != nil {
		fmt.Printf("读取join响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到join响应: %s\n", message)

	// 等待一会儿，让服务端处理完成
	time.Sleep(1 * time.Second)

	// 2. 测试push请求
	fmt.Println("\n=== 测试2: push请求 ===")
	pushRequest := map[string]interface{}{
		"request": true,
		"id":      1002,
		"method":  "push",
		"data": map[string]interface{}{
			"roomId":   "testroom",
			"userId":   "testuser",
			"streamId": "stream-001",
			"type":     "screen",
		},
	}
	
	err = c.WriteJSON(pushRequest)
	if err != nil {
		fmt.Printf("发送push请求失败: %v\n", err)
		return
	}
	fmt.Println("发送push请求成功")

	_, message, err = c.ReadMessage()
	if err != nil {
		fmt.Printf("读取push响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到push响应: %s\n", message)

	// 等待一会儿，让服务端处理完成
	time.Sleep(1 * time.Second)

	// 3. 测试pull请求
	fmt.Println("\n=== 测试3: pull请求 ===")
	pullRequest := map[string]interface{}{
		"request": true,
		"id":      1003,
		"method":  "pull",
		"data": map[string]interface{}{
			"roomId":   "testroom",
			"userId":   "testuser",
			"streamId": "stream-001",
			"source":   "testuser",
		},
	}
	
	err = c.WriteJSON(pullRequest)
	if err != nil {
		fmt.Printf("发送pull请求失败: %v\n", err)
		return
	}
	fmt.Println("发送pull请求成功")

	_, message, err = c.ReadMessage()
	if err != nil {
		fmt.Printf("读取pull响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到pull响应: %s\n", message)

	// 等待一会儿，让服务端处理完成
	time.Sleep(1 * time.Second)

	// 4. 测试leave请求
	fmt.Println("\n=== 测试4: leave请求 ===")
	leaveRequest := map[string]interface{}{
		"request": true,
		"id":      1004,
		"method":  "leave",
		"data": map[string]interface{}{
			"roomId": "testroom",
			"userId": "testuser",
		},
	}
	
	err = c.WriteJSON(leaveRequest)
	if err != nil {
		fmt.Printf("发送leave请求失败: %v\n", err)
		return
	}
	fmt.Println("发送leave请求成功")

	_, message, err = c.ReadMessage()
	if err != nil {
		fmt.Printf("读取leave响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到leave响应: %s\n", message)

	fmt.Println("\n=== 所有测试完成 ===")
}

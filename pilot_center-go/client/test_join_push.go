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

	// 测试join请求
	fmt.Println("\n=== 测试join请求 ===")
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

	// 等待3秒，让服务端处理完成
	time.Sleep(3 * time.Second)

	// 测试push请求
	fmt.Println("\n=== 测试push请求 ===")
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

	// 等待3秒，让服务端处理完成
	time.Sleep(3 * time.Second)

	fmt.Println("\n=== 测试完成 ===")
	fmt.Println("保持连接打开5秒...")
	time.Sleep(5 * time.Second)
	fmt.Println("关闭连接")
}

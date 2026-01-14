package main

import (
	"fmt"
	"net/url"
	"os"
	"time"

	"github.com/gorilla/websocket"
)

func main() {
	// 确保输出被立即打印
	os.Stdout.Sync()
	os.Stderr.Sync()

	fmt.Println("===== 开始测试 =====")

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
	fmt.Println("正在建立连接...")
	c, resp, err := dialer.Dial(u.String(), nil)
	if err != nil {
		fmt.Printf("连接失败: %v\n", err)
		if resp != nil {
			fmt.Printf("响应状态: %s\n", resp.Status)
			fmt.Printf("响应头:\n")
			for key, values := range resp.Header {
				for _, value := range values {
					fmt.Printf("%s: %s\n", key, value)
				}
			}
		}
		return
	}
	defer c.Close()

	fmt.Println("连接成功!")

	// 测试join请求
	fmt.Println("\n发送join请求...")
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

	fmt.Println("等待join响应...")
	_, message, err := c.ReadMessage()
	if err != nil {
		fmt.Printf("读取join响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到join响应: %s\n", message)

	// 测试push请求
	fmt.Println("\n发送push请求...")
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

	fmt.Println("等待push响应...")
	_, message, err = c.ReadMessage()
	if err != nil {
		fmt.Printf("读取push响应失败: %v\n", err)
		return
	}
	fmt.Printf("接收到push响应: %s\n", message)

	fmt.Println("\n===== 测试完成 =====")
}

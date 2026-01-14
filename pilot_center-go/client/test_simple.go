package main

import (
	"fmt"
	"net/url"
	"os"
	"os/signal"
	"time"

	"github.com/gorilla/websocket"
)

func main() {
	// WebSocket服务器地址
	u := url.URL{Scheme: "ws", Host: "localhost:8080", Path: "/pilot/center"}
	fmt.Printf("正在尝试连接到: %s\n", u.String())

	// 配置WebSocket拨号器
	dialer := &websocket.Dialer{
		ReadBufferSize:   1024,
		WriteBufferSize:  1024,
		HandshakeTimeout: 10 * time.Second,
	}

	// 尝试连接
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

	// 发送join请求
	joinRequest := map[string]interface{}{
		"request": true,
		"id":      12345,
		"method":  "join",
		"data": map[string]interface{}{
			"roomId":   "testroom",
			"userId":   "testuser",
			"userName": "Test User",
		},
	}

	err = c.WriteJSON(joinRequest)
	if err != nil {
		fmt.Printf("发送请求失败: %v\n", err)
		return
	}
	fmt.Println("发送join请求成功")

	// 等待响应
	fmt.Println("等待响应...")
	_, message, err := c.ReadMessage()
	if err != nil {
		fmt.Printf("读取响应失败: %v\n", err)
		return
	}

	fmt.Printf("接收到响应: %s\n", message)

	// 等待中断信号
	fmt.Println("按Ctrl+C退出...")
	interrupt := make(chan os.Signal, 1)
	signal.Notify(interrupt, os.Interrupt)
	<-interrupt

	fmt.Println("正在关闭连接...")
}

package main

import (
	"fmt"
	"net/http"
	"time"
)

func main() {
	// 创建一个HTTP客户端，设置超时时间
	client := &http.Client{
		Timeout: 5 * time.Second,
	}

	// 发送GET请求到WebSocket升级端点
	resp, err := client.Get("http://localhost:4443/pilot/center")
	if err != nil {
		fmt.Printf("请求失败: %v\n", err)
		return
	}
	defer resp.Body.Close()

	fmt.Printf("响应状态: %s\n", resp.Status)
	fmt.Printf("响应头:\n")
	for key, values := range resp.Header {
		for _, value := range values {
			fmt.Printf("%s: %s\n", key, value)
		}
	}
}

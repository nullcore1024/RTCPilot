package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"pilot_center-go/config"
	"pilot_center-go/logger"
	"pilot_center-go/msu"
	"pilot_center-go/room"
	"pilot_center-go/websocket_protoo"
)

func main() {
	// 解析命令行参数
	configPath := flag.String("config", "", "配置文件路径")
	logFile := flag.String("log-file", "", "日志文件路径")
	flag.Parse()

	// 检查配置文件路径
	if 0 == len(*configPath) {
		fmt.Fprintln(os.Stderr, "错误: 必须指定配置文件路径")
		flag.Usage()
		os.Exit(1)
	}

	// 初始化日志系统
	log := logger.GetLogger()
	if 0 != len(*logFile) {
		if err := log.SetOutput(*logFile); err != nil {
			fmt.Fprintf(os.Stderr, "错误: 设置日志文件失败: %v\n", err)
			os.Exit(1)
		}
	}

	log.Info("正在启动 Pilot Center...")

	// 加载配置文件
	cfg, err := config.LoadConfig(*configPath)
	if err != nil {
		log.Errorf("加载配置文件失败: %v", err)
		os.Exit(1)
	}

	log.Infof("加载配置成功:\n%s", cfg.String())

	// 创建房间管理器
	roomMgr := room.NewRoomManager()

	// 创建MSU管理器
	msuMgr := msu.NewMsuManager()

	// 创建WebSocket服务器选项
	opts := websocket_protoo.ServerOptions{
		Host:           cfg.WebSocket.ListenIP,
		Port:           cfg.WebSocket.ListenPort,
		CertPath:       cfg.WebSocket.CertPath,
		KeyPath:        cfg.WebSocket.KeyPath,
		Subpath:        cfg.WebSocket.Subpath,
		ReadBufferSize: cfg.WebSocket.ReadBufferSize,
		WriteBufferSize: cfg.WebSocket.WriteBufferSize,
		ReadTimeout:    cfg.WebSocket.ReadTimeout,
		WriteTimeout:   cfg.WebSocket.WriteTimeout,
	}

	// 创建WebSocket服务器
	server := websocket_protoo.NewServer(opts, roomMgr, msuMgr)

	// 启动服务器（在goroutine中）
	go func() {
		if err := server.Start(); err != nil {
			log.Errorf("启动WebSocket服务器失败: %v", err)
			os.Exit(1)
		}
	}()

	// 等待中断信号
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	// 关闭服务器
	log.Info("正在关闭 Pilot Center...")
	if err := server.Stop(); err != nil {
		log.Errorf("关闭WebSocket服务器失败: %v", err)
	}

	log.Info("Pilot Center 已关闭")
}

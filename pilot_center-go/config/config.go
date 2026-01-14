package config

import (
	"fmt"
	"os"
	"path/filepath"

	"gopkg.in/yaml.v3"
)

// Config 表示WebSocket Protoo中心的配置
type Config struct {
	WebSocket WebSocketConfig `yaml:"websocket"`
}

// WebSocketConfig 表示WebSocket相关的配置
type WebSocketConfig struct {
	ListenIP       string `yaml:"listen_ip"`
	ListenPort     int    `yaml:"listen_port"`
	CertPath       string `yaml:"cert_path"`
	KeyPath        string `yaml:"key_path"`
	Subpath        string `yaml:"subpath"`
	ReadBufferSize int    `yaml:"read_buffer_size"`
	WriteBufferSize int   `yaml:"write_buffer_size"`
	ReadTimeout    int    `yaml:"read_timeout"`
	WriteTimeout   int    `yaml:"write_timeout"`
}

// LoadConfig 从YAML文件加载配置
func LoadConfig(path string) (*Config, error) {
	// 检查文件是否存在
	if _, err := os.Stat(path); os.IsNotExist(err) {
		return nil, fmt.Errorf("配置文件不存在: %s", path)
	}

	// 读取文件内容
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("读取配置文件失败: %w", err)
	}

	// 解析YAML
	var config Config
	if err := yaml.Unmarshal(data, &config); err != nil {
		return nil, fmt.Errorf("解析YAML配置失败: %w", err)
	}

	// 验证必要的配置项
	if 0 == len(config.WebSocket.ListenIP) {
		return nil, fmt.Errorf("缺少listen_ip配置")
	}
	if 0 == config.WebSocket.ListenPort {
		return nil, fmt.Errorf("缺少listen_port配置")
	}

	// 设置默认值
	if 0 == len(config.WebSocket.Subpath) {
		config.WebSocket.Subpath = "/pilot/center"
	}
	if 0 == config.WebSocket.ReadBufferSize {
		config.WebSocket.ReadBufferSize = 2048
	}
	if 0 == config.WebSocket.WriteBufferSize {
		config.WebSocket.WriteBufferSize = 2048
	}
	if 0 == config.WebSocket.ReadTimeout {
		config.WebSocket.ReadTimeout = 30
	}
	if 0 == config.WebSocket.WriteTimeout {
		config.WebSocket.WriteTimeout = 30
	}

	// 解析证书路径和密钥路径为绝对路径
	yamlDir := filepath.Dir(path)
	if 0 != len(config.WebSocket.CertPath) && !filepath.IsAbs(config.WebSocket.CertPath) {
		config.WebSocket.CertPath = filepath.Join(yamlDir, config.WebSocket.CertPath)
	}
	if 0 != len(config.WebSocket.KeyPath) && !filepath.IsAbs(config.WebSocket.KeyPath) {
		config.WebSocket.KeyPath = filepath.Join(yamlDir, config.WebSocket.KeyPath)
	}

	return &config, nil
}

// String 返回配置的字符串表示
func (c *Config) String() string {
	return fmt.Sprintf("websocket:\n  listen_ip: %s\n  listen_port: %d\n  cert_path: %s\n  key_path: %s\n  subpath: %s\n  read_buffer_size: %d\n  write_buffer_size: %d\n  read_timeout: %d\n  write_timeout: %d",
		c.WebSocket.ListenIP, c.WebSocket.ListenPort, c.WebSocket.CertPath, c.WebSocket.KeyPath, c.WebSocket.Subpath,
		c.WebSocket.ReadBufferSize, c.WebSocket.WriteBufferSize,
		c.WebSocket.ReadTimeout, c.WebSocket.WriteTimeout)
}

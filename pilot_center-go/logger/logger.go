package logger

import (
	"google.golang.org/grpc/grpclog"
	"io"
	"os"
	"sync"
)

// Logger 是一个全局日志记录器
type Logger struct {
	logger grpclog.LoggerV2
	mu     sync.Mutex
}

var (
	globalLogger *Logger
	once         sync.Once
)

// Init 初始化全局日志记录器
func Init() *Logger {
	once.Do(func() {
		globalLogger = &Logger{
			logger: grpclog.NewLoggerV2(os.Stdout, os.Stderr, os.Stderr),
		}
	})
	return globalLogger
}

// GetLogger 返回全局日志记录器的实例
func GetLogger() *Logger {
	if globalLogger == nil {
		return Init()
	}
	return globalLogger
}

// SetOutput 设置日志输出到文件
func (l *Logger) SetOutput(filePath string) error {
	l.mu.Lock()
	defer l.mu.Unlock()

	// 打开日志文件
	file, err := os.OpenFile(filePath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return err
	}

	// 创建一个同时输出到控制台和文件的writer
	writer := io.MultiWriter(os.Stdout, file)

	// 创建新的日志记录器
	l.logger = grpclog.NewLoggerV2(writer, writer, writer)

	return nil
}

// Debug 打印调试级别的日志
func (l *Logger) Debug(args ...interface{}) {
	l.logger.Info(args...)
}

// Debugf 打印格式化的调试级别的日志
func (l *Logger) Debugf(format string, args ...interface{}) {
	l.logger.Infof(format, args...)
}

// Info 打印信息级别的日志
func (l *Logger) Info(args ...interface{}) {
	l.logger.Info(args...)
}

// Infof 打印格式化的信息级别的日志
func (l *Logger) Infof(format string, args ...interface{}) {
	l.logger.Infof(format, args...)
}

// Warning 打印警告级别的日志
func (l *Logger) Warning(args ...interface{}) {
	l.logger.Warning(args...)
}

// Warningf 打印格式化的警告级别的日志
func (l *Logger) Warningf(format string, args ...interface{}) {
	l.logger.Warningf(format, args...)
}

// Error 打印错误级别的日志
func (l *Logger) Error(args ...interface{}) {
	l.logger.Error(args...)
}

// Errorf 打印格式化的错误级别的日志
func (l *Logger) Errorf(format string, args ...interface{}) {
	l.logger.Errorf(format, args...)
}

// Fatal 打印致命错误级别的日志并退出程序
func (l *Logger) Fatal(args ...interface{}) {
	l.logger.Fatal(args...)
}

// Fatalf 打印格式化的致命错误级别的日志并退出程序
func (l *Logger) Fatalf(format string, args ...interface{}) {
	l.logger.Fatalf(format, args...)
}

// 全局日志函数，方便直接调用

// Debug 打印调试级别的日志
func Debug(args ...interface{}) {
	GetLogger().Debug(args...)
}

// Debugf 打印格式化的调试级别的日志
func Debugf(format string, args ...interface{}) {
	GetLogger().Debugf(format, args...)
}

// Info 打印信息级别的日志
func Info(args ...interface{}) {
	GetLogger().Info(args...)
}

// Infof 打印格式化的信息级别的日志
func Infof(format string, args ...interface{}) {
	GetLogger().Infof(format, args...)
}

// Warning 打印警告级别的日志
func Warning(args ...interface{}) {
	GetLogger().Warning(args...)
}

// Warningf 打印格式化的警告级别的日志
func Warningf(format string, args ...interface{}) {
	GetLogger().Warningf(format, args...)
}

// Error 打印错误级别的日志
func Error(args ...interface{}) {
	GetLogger().Error(args...)
}

// Errorf 打印格式化的错误级别的日志
func Errorf(format string, args ...interface{}) {
	GetLogger().Errorf(format, args...)
}

// Fatal 打印致命错误级别的日志并退出程序
func Fatal(args ...interface{}) {
	GetLogger().Fatal(args...)
}

// Fatalf 打印格式化的致命错误级别的日志并退出程序
func Fatalf(format string, args ...interface{}) {
	GetLogger().Fatalf(format, args...)
}

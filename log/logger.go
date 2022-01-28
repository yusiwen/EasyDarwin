package log

import (
	"fmt"
	"io"
)

type LoggerType int

const (
	SessionId LoggerType = iota
	ClientId
)

func (l LoggerType) String() string {
	switch l {
	case SessionId:
		return "sessionId"
	case ClientId:
		return "clientId"
	}
	return ""
}

type Logger struct {
	id         string
	loggerType LoggerType
}

func NewLogger(sessionId string, loggerType LoggerType) *Logger {
	return &Logger{
		id:         sessionId,
		loggerType: loggerType,
	}
}

func (s *Logger) SetOutput(o io.Writer) {
	SetOutput(o)
}

// Debug logs a message at level Debug on the standard logger.
func (s *Logger) Debug(args ...interface{}) {
	Debug(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

// Info logs a message at level Info on the standard logger.
func (s *Logger) Info(args ...interface{}) {
	Info(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

// Warn logs a message at level Warn on the standard logger.
func (s *Logger) Warn(args ...interface{}) {
	Warn(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

// Error logs a message at level Error on the standard logger.
func (s *Logger) Error(args ...interface{}) {
	Error(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

// Fatal logs a message at level Fatal on the standard logger.
func (s *Logger) Fatal(args ...interface{}) {
	Fatal(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

// Panic logs a message at level Panic on the standard logger.
func (s *Logger) Panic(args ...interface{}) {
	Panic(fmt.Sprintf("[%s: %s]", s.loggerType, s.id), args)
}

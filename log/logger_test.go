package log

import (
	"testing"
)

func TestLogger(t *testing.T) {
	d := "Hello"
	logger := NewLogger("1234", SessionId)
	logger.Info("Test Message: ", d)

	logger = NewLogger("1234", ClientId)
	logger.Info("Test Message: ", d)
}

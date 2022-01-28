package log

import (
	"errors"
	easy "github.com/t-tomalak/logrus-easy-formatter"
	"testing"
)

func TestLog(t *testing.T) {
	d := "Hello"
	Debug("Debug: ", d)
	Info("Info: ", d)
	Info("Info2: ", d)
	Error("Error: ", errors.New("Test error"))

	SetLogFormatter(&easy.Formatter{
		TimestampFormat: "2006-01-02 15:04:05",
		LogFormat:       "[%time%][%lvl%][%file%][%sessionId%]: %msg%\n",
	})

	InfoWithFields("hfhgh", Fields{"sessionId": "fhghdfh12d"})
}

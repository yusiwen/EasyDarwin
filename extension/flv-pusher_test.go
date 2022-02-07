package extension

import (
	"fmt"
	ffmpeg "github.com/u2takey/ffmpeg-go"
	"strings"
	"testing"
)

func TestFFMpegCmd(t *testing.T) {
	ffmpegArgs := ffmpeg.KwArgs{"c:v": "copy", "c:a": "aac", "f": "flv"}
	stream := ffmpeg.Input("rtsp://localhost:18554/sample01", ffmpeg.KwArgs{"rtsp_transport": "tcp"}).
		Output("rtmp://localhost:1935/live/rfBd56ti2SMtYvSgD5xAV0YU99zampta7Z7S575KLkIZ9PYk", ffmpegArgs)
	stream = stream.GlobalArgs(strings.Split("-hide_banner -nostdin -progress - -nostats", " ")...)
	stream = stream.OverWriteOutput().ErrorToStdOut()
	fmt.Sprintln(stream.Compile().String())
}

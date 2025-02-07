package rtsp

import (
	"context"
	"errors"
	"strings"
	"sync"

	"github.com/EasyDarwin/EasyDarwin/log"
	config "github.com/MeloQi/EasyGoLib/utils"
	ffmpeg "github.com/u2takey/ffmpeg-go"
)

type RecorderStopHandler func(r *Recorder)

type RecordFormat int

const (
	Mkv RecordFormat = iota
	M3u8
	Jpeg
)

func (f RecordFormat) String() string {
	return [...]string{"mkv", "m3u8", "jpeg"}[f]
}

func ParseRecordFormat(s string) RecordFormat {
	s = strings.ToLower(s)
	switch s {
	case "mkv":
		return Mkv
	case "jpeg":
		return Mkv
	default:
		return M3u8
	}
}

type Recorder struct {
	Id           string
	URL          string
	OutputFormat RecordFormat
	File         string
	StopHandler  RecorderStopHandler

	running bool
	lock    sync.Mutex
	stopped chan bool
	cancel  context.CancelFunc
}

func NewRecorder(id string, url string, output string, handler RecorderStopHandler) *Recorder {
	return &Recorder{
		Id:           id,
		URL:          url,
		File:         output,
		OutputFormat: M3u8,
		StopHandler:  handler,

		running: false,
		stopped: make(chan bool),
		cancel:  nil,
	}
}

func (r *Recorder) Start() error {
	r.lock.Lock()
	defer r.lock.Unlock()
	if !r.running {
		r.running = true

		go func() {
			defer func() {
				// set end time of this recording
				if r.StopHandler != nil {
					r.StopHandler(r)
				}

				r.lock.Lock()
				r.running = false
				r.lock.Unlock()
				r.stopped <- true
			}()

			// start recording
			ffmpegCmd := config.Conf().Section("codec").Key("ffmpeg_binary").MustString("ffmpeg")
			outputArgs := ffmpeg.KwArgs{}
			if r.OutputFormat == Jpeg {
				// Screenshot
				outputArgs["vframes"] = "1"
			} else {
				// Video recording
				outputArgs["c:v"] = "copy"
				outputArgs["c:a"] = "copy"
				if r.OutputFormat == M3u8 {
					outputArgs["hls_time"] = config.Conf().Section("record").Key("ts_duration_second").MustString("60")
					outputArgs["hls_list_size"] = "0"
				}
			}

			stream := ffmpeg.Input(r.URL, ffmpeg.KwArgs{"re": "", "rtsp_transport": "tcp"}).
				Output(r.File, outputArgs).
				GlobalArgs("-hide_banner", "-nostats", "-nostdin", "-loglevel", "error").
				ErrorToStdOut()
			r.cancel = stream.GetCancelFunc()

			err := stream.RunWith(ffmpegCmd)
			if err != nil {
				log.Error("failed to start ffmpeg: ", err)
			}
		}()
		return nil
	} else {
		return errors.New("recorder is already running")
	}
}

func (r *Recorder) Stop() {
	if r != nil && r.cancel != nil {
		r.cancel()
	}
}

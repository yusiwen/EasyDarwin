package extension

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/EasyDarwin/EasyDarwin/utils"
	config "github.com/MeloQi/EasyGoLib/utils"
	u "github.com/MeloQi/EasyGoLib/utils"
	ffmpeg "github.com/u2takey/ffmpeg-go"
	"gopkg.in/natefinch/lumberjack.v2"

	"io/ioutil"
	"net/http"
	"path"
	"strings"
)

type getRe struct {
	Status int    `json:"status"`
	Data   string `json:"data"`
}

type FlvPusher struct {
	*FlvServer
	SourceUrl    string
	SourceACodec string
	SourceVCodec string
	AppName      string
	RoomName     string
	Cancel       context.CancelFunc
}

func NewFlvPusher(source string, acodec string, vcodec string, flvServer *FlvServer, app string, room string) *FlvPusher {
	pusher := &FlvPusher{
		SourceUrl:    source,
		SourceACodec: acodec,
		SourceVCodec: vcodec,
		FlvServer:    flvServer,
		AppName:      app,
		RoomName:     room,
		Cancel:       nil,
	}
	return pusher
}

func (s *FlvPusher) getFlvPushUrl() (string, error) {
	apiUrl := utils.GetFullAddress(s.FlvServer.ApiAddr)

	roomKeyPath := fmt.Sprintf("http://%s/control/get?room=%v", apiUrl, s.RoomName)
	response, err := http.Get(roomKeyPath)
	if err != nil {
		return "", err
	}
	defer response.Body.Close()
	body, err := ioutil.ReadAll(response.Body)
	if err != nil {
		return "", err
	}

	var re getRe
	err = json.Unmarshal(body, &re)
	if err != nil {
		return "", err
	}

	streamUrl := utils.GetFullAddress(s.FlvServer.StreamAddr)
	customPath := fmt.Sprintf("rtmp://%s/%s/%s", streamUrl, s.AppName, re.Data)
	return customPath, nil
}

func (s *FlvPusher) Start() error {
	pushUrl, err := s.getFlvPushUrl()
	if err != nil {
		return err
	}

	go func() {
		ffmpegCmd := config.Conf().Section("codec").Key("ffmpeg_binary").MustString("ffmpeg")
		globalArgs := config.Conf().Section("codec").Key("ffmpeg_general_options").MustString("")
		ffmpegArgs := ffmpeg.KwArgs{"c:a": "aac", "f": "flv"}
		if strings.EqualFold(s.SourceVCodec, "H264") {
			ffmpegArgs["c:v"] = "copy"
		} else {
			ffmpegArgs["c:v"] = config.Conf().Section("codec").Key("ffmpeg_video_codec").MustString("libx264")
		}

		stream := ffmpeg.Input(s.SourceUrl, ffmpeg.KwArgs{"rtsp_transport": "tcp"}).
			Output(pushUrl, ffmpegArgs)
		if strings.TrimSpace(globalArgs) != "" {
			stream = stream.GlobalArgs(strings.Split(globalArgs, " ")...)
		}
		stream = stream.OverWriteOutput()

		// ffmpeg log file
		fn := fmt.Sprintf("ffmpeg-%s-%s.log", s.AppName, s.RoomName)
		lp := config.Conf().Section("codec").Key("ffmpeg_log_dir").MustString(u.CWD())
		out := &lumberjack.Logger{
			Filename:   path.Join(lp, fn),
			MaxSize:    config.Conf().Section("codec").Key("ffmpeg_log_max_size").MustInt(100), // MB
			MaxBackups: config.Conf().Section("codec").Key("ffmpeg_log_max_backups").MustInt(100),
			MaxAge:     config.Conf().Section("codec").Key("ffmpeg_log_max_age").MustInt(100),     // days
			Compress:   config.Conf().Section("codec").Key("ffmpeg_log_compress").MustBool(false), // disabled by default
		}
		stream = stream.WithOutput(out).WithErrorOutput(out)

		s.Cancel = stream.GetCancelFunc()

		log.Info(fmt.Sprintf("FlvPusher[%s][%s] starting...", s.AppName, s.RoomName))
		err = stream.RunWith(ffmpegCmd)
		if err != nil {
			log.Error("failed to start ffmpeg: ", err)
		}
		log.Info(fmt.Sprintf("FlvPusher[%s][%s] finished...", s.AppName, s.RoomName))
	}()
	return nil
}

func (s *FlvPusher) Stop() error {
	if s != nil && s.Cancel != nil {
		s.Cancel()
	}
	return nil
}

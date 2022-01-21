package extension

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"

	"github.com/EasyDarwin/EasyDarwin/utils"
	config "github.com/MeloQi/EasyGoLib/utils"
	ffmpeg "github.com/u2takey/ffmpeg-go"
)

type getRe struct {
	Status int    `json:"status"`
	Data   string `json:"data"`
}

type FlvPusher struct {
	*FlvServer
	SourceUrl string
	AppName   string
	RoomName  string
	Logger    *log.Logger
	Context   context.Context
	Cancel    context.CancelFunc
}

func NewFlvPusher(source string, flvServer *FlvServer, app string, room string) *FlvPusher {
	pusher := &FlvPusher{
		SourceUrl: source,
		FlvServer: flvServer,
		AppName:   app,
		RoomName:  room,
		Logger:    log.New(os.Stdout, "[FlvPusher] ", log.LstdFlags|log.Lshortfile),
		Context:   nil,
		Cancel:    nil,
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
	ctx, cancel := context.WithCancel(context.Background())
	s.Context = ctx
	s.Cancel = cancel
	go func() {
		c := ffmpeg.Input(s.SourceUrl, ffmpeg.KwArgs{"rtsp_transport": "tcp"}).
			Output(pushUrl, ffmpeg.KwArgs{"c": "copy", "c:a": "aac", "f": "flv"}).Compile()
		s.Logger.Println("flvpusher starting...")
		ffmpegCmd := "d:\\opt\\apps\\ffmpeg-4.4.1-full_build\\bin\\ffmpeg.exe"
		if !utils.CommandExists(ffmpegCmd) {
			ffmpegCmd = config.Conf().Section("codec").Key("ffmpeg_binary").MustString("ffmpeg")
		}

		cmd := exec.CommandContext(s.Context, ffmpegCmd, c.Args[1:]...)
		if err := cmd.Run(); err != nil {
			s.Logger.Println(err)
		}
		s.Logger.Println("flvpusher finished")
	}()
	return nil
}

func (s *FlvPusher) Stop() error {
	if s != nil {
		s.Cancel()
	}
	return nil
}

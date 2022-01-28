package extension

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/EasyDarwin/EasyDarwin/utils"
	config "github.com/MeloQi/EasyGoLib/utils"
	ffmpeg "github.com/u2takey/ffmpeg-go"
	"io/ioutil"
	"net/http"
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
	Cancel    context.CancelFunc
}

func NewFlvPusher(source string, flvServer *FlvServer, app string, room string) *FlvPusher {
	pusher := &FlvPusher{
		SourceUrl: source,
		FlvServer: flvServer,
		AppName:   app,
		RoomName:  room,
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

	go func() {
		ffmpegCmd := "ffmpeg"
		if !utils.CommandExists(ffmpegCmd) {
			ffmpegCmd = config.Conf().Section("codec").Key("ffmpeg_binary").MustString("ffmpeg")
		}

		stream := ffmpeg.Input(s.SourceUrl, ffmpeg.KwArgs{"rtsp_transport": "tcp"}).
			Output(pushUrl, ffmpeg.KwArgs{"c": "copy", "c:a": "aac", "f": "flv"}).
			OverWriteOutput().ErrorToStdOut()
		s.Cancel = stream.GetCancelFunc()

		log.Info("flvpusher starting...")
		err := stream.RunWith(ffmpegCmd)
		if err != nil {
			log.Error("failed to start ffmpeg: ", err)
		}
		log.Info("flvpusher finished")
	}()
	return nil
}

func (s *FlvPusher) Stop() error {
	if s != nil {
		s.Cancel()
	}
	return nil
}

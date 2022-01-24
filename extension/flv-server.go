package extension

import (
	"fmt"
	"github.com/MeloQi/EasyGoLib/utils"
	"github.com/gwuhaolin/livego/configure"
	"github.com/gwuhaolin/livego/protocol/api"
	"github.com/gwuhaolin/livego/protocol/hls"
	"github.com/gwuhaolin/livego/protocol/httpflv"
	"github.com/gwuhaolin/livego/protocol/rtmp"
	"net"
	"path"
	"runtime"
	"time"

	log "github.com/sirupsen/logrus"
)

type FlvServer struct {
	ApiAddr        string
	StreamAddr     string
	FlvAddr        string
	HlsAddr        string
	Enabled        bool
	Embedded       bool
	LogLevel       string
	ReadTimeout    int
	WriteTimeout   int
	ApiListener    *net.TCPListener
	StreamListener *net.TCPListener
	FlvListener    *net.TCPListener
	HlsListener    *net.TCPListener
}

var instance *FlvServer = nil

func GetFlvServer() *FlvServer {
	if instance == nil {
		instance = &FlvServer{
			Enabled:        utils.Conf().Section("flv").Key("enabled").MustBool(true),
			Embedded:       utils.Conf().Section("flv").Key("embedded_server").MustBool(true),
			LogLevel:       utils.Conf().Section("flv").Key("log_level").MustString("info"),
			ApiAddr:        utils.Conf().Section("flv").Key("api_addr").MustString(":8090"),
			StreamAddr:     utils.Conf().Section("flv").Key("stream_addr").MustString(":1935"),
			FlvAddr:        utils.Conf().Section("flv").Key("flv_addr").MustString(":7001"),
			HlsAddr:        utils.Conf().Section("flv").Key("hls_addr").MustString(":7002"),
			ReadTimeout:    utils.Conf().Section("flv").Key("read_timeout").MustInt(10),
			WriteTimeout:   utils.Conf().Section("flv").Key("write_timeout").MustInt(10),
			ApiListener:    nil,
			StreamListener: nil,
			FlvListener:    nil,
			HlsListener:    nil,
		}

		configure.Config.Set("read_timeout", instance.ReadTimeout)
		configure.Config.Set("write_timeout", instance.ReadTimeout)
		configure.Config.Set("level", instance.LogLevel)
		configure.Config.Set("rtmp_addr", instance.StreamAddr)
		configure.Config.Set("httpflv_addr", instance.FlvAddr)
		configure.Config.Set("api_addr", instance.ApiAddr)

		// Log
		if l, err := log.ParseLevel(configure.Config.GetString("level")); err == nil {
			log.SetLevel(l)
			log.SetReportCaller(l == log.DebugLevel)
		}
	}
	return instance
}

func init() {
	log.SetFormatter(&log.TextFormatter{
		FullTimestamp: true,
		CallerPrettyfier: func(f *runtime.Frame) (string, string) {
			filename := path.Base(f.File)
			return fmt.Sprintf("%s()", f.Function), fmt.Sprintf(" %s:%d", filename, f.Line)
		},
	})
}

func (s *FlvServer) startHls() *hls.Server {
	addr, err := net.ResolveTCPAddr("tcp", s.HlsAddr)
	if err != nil {
		log.Fatal(err)
	}
	hlsListen, err := net.ListenTCP("tcp", addr)
	if err != nil {
		log.Fatal(err)
	}

	s.HlsListener = hlsListen
	hlsServer := hls.NewServer()
	go func() {
		defer func() {
			if r := recover(); r != nil {
				log.Error("HLS server panic: ", r)
			}
		}()
		log.Info("HLS listen On ", s.HlsAddr)
		hlsServer.Serve(hlsListen)
	}()
	return hlsServer
}

func (s *FlvServer) startRtmp(stream *rtmp.RtmpStream, hlsServer *hls.Server) {
	addr, err := net.ResolveTCPAddr("tcp", s.StreamAddr)
	if err != nil {
		log.Fatal(err)
	}
	rtmpListen, err := net.ListenTCP("tcp", addr)
	if err != nil {
		log.Fatal(err)
	}

	var rtmpServer *rtmp.Server

	if hlsServer == nil {
		rtmpServer = rtmp.NewRtmpServer(stream, nil)
		log.Info("HLS server disabled....")
	} else {
		rtmpServer = rtmp.NewRtmpServer(stream, hlsServer)
		log.Info("HLS server enabled....")
	}

	defer func() {
		if r := recover(); r != nil {
			log.Error("RTMP server panic: ", r)
		}
	}()
	log.Info("RTMP Listen On ", s.StreamAddr)
	s.StreamListener = rtmpListen
	rtmpServer.Serve(rtmpListen)
}

func (s *FlvServer) startHTTPFlv(stream *rtmp.RtmpStream) {
	addr, err := net.ResolveTCPAddr("tcp", s.FlvAddr)
	if err != nil {
		log.Fatal(err)
	}
	flvListen, err := net.ListenTCP("tcp", addr)
	if err != nil {
		log.Fatal(err)
	}

	s.FlvListener = flvListen
	hdlServer := httpflv.NewServer(stream)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				log.Error("HTTP-FLV server panic: ", r)
			}
		}()
		log.Info("HTTP-FLV listen On ", s.FlvAddr)
		hdlServer.Serve(flvListen)
	}()
}

func (s *FlvServer) startAPI(stream *rtmp.RtmpStream) {
	apiAddr := s.ApiAddr
	if apiAddr != "" {
		addr, err := net.ResolveTCPAddr("tcp", apiAddr)
		if err != nil {
			log.Fatal(err)
		}
		opListen, err := net.ListenTCP("tcp", addr)
		if err != nil {
			log.Fatal(err)
		}
		s.ApiListener = opListen
		opServer := api.NewServer(stream, s.StreamAddr)
		go func() {
			defer func() {
				if r := recover(); r != nil {
					log.Error("HTTP-API server panic: ", r)
				}
			}()
			log.Info("HTTP-API listen On ", apiAddr)
			opServer.Serve(opListen)
		}()
	}
}

func (s *FlvServer) Start() {

	defer func() {
		if r := recover(); r != nil {
			log.Error("livego panic: ", r)
			time.Sleep(1 * time.Second)
		}
	}()

	apps := configure.Applications{}
	configure.Config.UnmarshalKey("server", &apps)
	for _, app := range apps {
		stream := rtmp.NewRtmpStream()
		var hlsServer *hls.Server
		if app.Hls {
			hlsServer = s.startHls()
		}
		if app.Flv {
			s.startHTTPFlv(stream)
		}
		if app.Api {
			s.startAPI(stream)
		}

		s.startRtmp(stream, hlsServer)
	}
}

func (s *FlvServer) Stop() {
	if s.ApiListener != nil {
		s.ApiListener.Close()
	}
	if s.HlsListener != nil {
		s.HlsListener.Close()
	}
	if s.FlvListener != nil {
		s.FlvListener.Close()
	}
	if s.StreamListener != nil {
		s.StreamListener.Close()
	}
}

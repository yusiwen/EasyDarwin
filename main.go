//go:generate go-bindata-assetfs -o bindata.go -prefix dist dist/...
package main

import (
	"context"
	"flag"
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/extension"
	"github.com/EasyDarwin/EasyDarwin/log"
	"net/http"
	"strings"
	"time"

	"github.com/EasyDarwin/EasyDarwin/models"
	"github.com/EasyDarwin/EasyDarwin/routers"
	"github.com/EasyDarwin/EasyDarwin/rtsp"
	"github.com/MeloQi/EasyGoLib/db"
	"github.com/MeloQi/EasyGoLib/utils"
	"github.com/MeloQi/service"
	"github.com/common-nighthawk/go-figure"
	assetfs "github.com/elazarl/go-bindata-assetfs"
)

var (
	gitCommitCode string
	buildDateTime string

	daemonExitCh chan struct{}
	daemonCancel context.CancelFunc
)

type program struct {
	httpPort   int
	httpServer *http.Server
	rtspPort   int
	rtspServer *rtsp.Server
	flvServer  *extension.FlvServer
	Stopped    bool
}

func (p *program) StopHTTP() (err error) {
	if p.httpServer == nil {
		err = fmt.Errorf("HTTP Server Not Found")
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err = p.httpServer.Shutdown(ctx); err != nil {
		return
	}
	return
}

func (p *program) StartHTTP() (err error) {
	p.httpServer = &http.Server{
		Addr:              fmt.Sprintf(":%d", p.httpPort),
		Handler:           routers.Router,
		ReadHeaderTimeout: 5 * time.Second,
	}
	link := fmt.Sprintf("http://%s:%d", utils.LocalIP(), p.httpPort)
	log.Info("http server start -->", link)
	go func() {
		if err := p.httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Error("start http server error: ", err)
		}
		log.Info("http server end")
	}()
	return
}

func (p *program) StartRTSP() (err error) {
	if p.rtspServer == nil {
		err = fmt.Errorf("RTSP Server Not Found")
		return
	}
	sport := ""
	if p.rtspPort != 554 {
		sport = fmt.Sprintf(":%d", p.rtspPort)
	}
	link := fmt.Sprintf("rtsp://%s%s", utils.LocalIP(), sport)
	log.Info("rtsp server start -->", link)
	go func() {
		if err := p.rtspServer.Start(); err != nil {
			log.Error("start rtsp server error: ", err)
		}
		log.Info("rtsp server end")
	}()
	return
}

func (p *program) StopRTSP() (err error) {
	if p.rtspServer == nil {
		err = fmt.Errorf("RTSP Server Not Found")
		return
	}
	p.rtspServer.Stop()
	return
}

func (p *program) StartFlvServer() (err error) {
	if p.flvServer.Enabled && p.flvServer.Embedded {
		p.flvServer.Start()
	}
	return nil
}

func (p *program) StopFlvServer() (err error) {
	if p.flvServer.Enabled && p.flvServer.Embedded {
		p.flvServer.Stop()
	}
	return nil
}

func (p *program) Start(s service.Service) (err error) {
	log.Info("********** START **********")
	if utils.IsPortInUse(p.httpPort) {
		err = fmt.Errorf("HTTP port[%d] In Use", p.httpPort)
		return
	}
	if utils.IsPortInUse(p.rtspPort) {
		err = fmt.Errorf("RTSP port[%d] In Use", p.rtspPort)
		return
	}
	err = models.Init()
	if err != nil {
		return
	}

	staticFS := assetfs.AssetFS{
		Asset:     Asset,
		AssetDir:  AssetDir,
		AssetInfo: AssetInfo,
		Prefix:    "/",
	}
	err = routers.Init(&staticFS)
	if err != nil {
		return
	}
	p.StartRTSP()
	p.StartHTTP()
	p.StartFlvServer()

	if !utils.Debug {
		log.Debug("log files -->", utils.LogDir())
		log.SetOutput(utils.GetLogWriter())
	}

	go func() {
		for range routers.API.RestartChan {
			p.StopHTTP()
			p.StopRTSP()
			p.StopFlvServer()
			utils.ReloadConf()
			p.StartRTSP()
			p.StartHTTP()
			p.StartFlvServer()
		}
	}()

	daemonExitCh = make(chan struct{})
	ctx, c := context.WithCancel(context.Background())
	daemonCancel = c
	go func(ctx context.Context) {
		log.Info("starting daemon for pulling streams")
		defer func() {
			log.Info("stopped daemon for pulling streams")
			daemonExitCh <- struct{}{}
		}()

		for {
			var streams []models.Stream
			result := db.SQL.Find(&streams, "status = ?", models.Running)
			if result.Error != nil {
				log.Error("find stream err: ", result.Error)
				return
			}
			for i := len(streams) - 1; i > -1; i-- {
				v := streams[i]
				if p := rtsp.GetServer().GetPusher(v.CustomPath); p != nil {
					if !p.Stopped() && p.FlvPusher.Stopped {
						log.Info(fmt.Sprintf("stopped flv-pusher detected [%v], restarting...", p))
						go p.FlvPusher.Start()
					}
					continue
				}
				agent := fmt.Sprintf("EasyDarwinGo/%s", routers.BuildVersion)
				if routers.BuildDateTime != "" {
					agent = fmt.Sprintf("%s(%s)", agent, routers.BuildDateTime)
				}
				client, err := rtsp.NewRTSPClient(v.ID, rtsp.GetServer(), v.URL, int64(v.HeartbeatInterval)*1000, agent)
				if err != nil {
					continue
				}
				client.CustomPath = v.CustomPath

				if v.Status == models.Running {
					err = client.Start(time.Duration(v.IdleTimeout) * time.Second)
					if err != nil {
						log.Error("pull stream err: ", err)
						continue
					}
				}

				flvPusher := extension.NewFlvPusher(client.URL, client.ACodec, client.VCodec, extension.GetFlvServer(),
					"live", client.ID)
				pusher := rtsp.NewClientPusher(client)
				pusher.FlvPusher = flvPusher
				if v.Status == models.Running {
					rtsp.GetServer().AddPusher(pusher, true)
				} else {
					rtsp.GetServer().AddPusher(pusher, false)
				}
			}
			//time.Sleep(10 * time.Second)
			t := time.NewTimer(10 * time.Second)
			select {
			case <-ctx.Done():
			case <-t.C:
			}

			if p.Stopped {
				break
			}
		}
	}(ctx)
	return
}

func (p *program) Stop(s service.Service) (err error) {
	defer log.Info("********** STOP **********")

	p.Stopped = true
	daemonCancel()

	p.StopHTTP()
	p.StopRTSP()
	p.StopFlvServer()
	models.Close()
	return
}

func main() {
	flag.StringVar(&utils.FlagVarConfFile, "config", "", "configure file path")
	flag.Parse()
	tail := flag.Args()

	log.Info("git commit code: ", gitCommitCode)
	log.Info("build date: ", buildDateTime)
	routers.BuildVersion = fmt.Sprintf("%s.%s", routers.BuildVersion, gitCommitCode)
	routers.BuildDateTime = buildDateTime

	sec := utils.Conf().Section("service")
	svcConfig := &service.Config{
		Name:        sec.Key("name").MustString("EasyDarwin_Service"),
		DisplayName: sec.Key("display_name").MustString("EasyDarwin_Service"),
		Description: sec.Key("description").MustString("EasyDarwin_Service"),
	}

	httpPort := utils.Conf().Section("http").Key("port").MustInt(10008)
	rtspServer := rtsp.GetServer()
	flvServer := extension.GetFlvServer()
	p := &program{
		httpPort:   httpPort,
		rtspPort:   rtspServer.TCPPort,
		rtspServer: rtspServer,
		flvServer:  flvServer,
		Stopped:    false,
	}
	s, err := service.New(p, svcConfig)
	if err != nil {
		log.Error(err)
		utils.PauseExit()
	}
	if len(tail) > 0 {
		cmd := strings.ToLower(tail[0])
		if cmd == "install" || cmd == "stop" || cmd == "start" || cmd == "uninstall" {
			figure.NewFigure("EasyDarwin", "", false).Print()
			log.Info(svcConfig.Name, cmd, "...")
			if err = service.Control(s, cmd); err != nil {
				log.Error(err)
				utils.PauseExit()
			}
			log.Info(svcConfig.Name, cmd, "ok")
			return
		}
	}
	figure.NewFigure("EasyDarwin", "", false).Print()
	if err = s.Run(); err != nil {
		log.Error(err)
		utils.PauseExit()
	}

	<-daemonExitCh
	utils.CloseLogWriter()
}

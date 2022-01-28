package rtsp

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/MeloQi/EasyGoLib/utils"
)

type Server struct {
	TCPListener    *net.TCPListener
	TCPPort        int
	Stopped        bool
	pushers        map[string]*Pusher // Path <-> Pusher
	pushersLock    sync.RWMutex
	addPusherCh    chan *Pusher
	removePusherCh chan *Pusher
}

var instance *Server = nil

func GetServer() *Server {
	if instance == nil {
		instance = &Server{
			Stopped:        true,
			TCPPort:        utils.Conf().Section("rtsp").Key("port").MustInt(554),
			pushers:        make(map[string]*Pusher),
			addPusherCh:    make(chan *Pusher),
			removePusherCh: make(chan *Pusher),
		}
	}
	return instance
}

func (server *Server) Start() (err error) {
	var (
		addr     *net.TCPAddr
		listener *net.TCPListener
	)
	if addr, err = net.ResolveTCPAddr("tcp", fmt.Sprintf(":%d", server.TCPPort)); err != nil {
		return
	}
	if listener, err = net.ListenTCP("tcp", addr); err != nil {
		return
	}

	localRecord := utils.Conf().Section("rtsp").Key("save_stream_to_local").MustInt(0)
	ffmpeg := utils.Conf().Section("rtsp").Key("ffmpeg_path").MustString("")
	m3u8DirPath := utils.Conf().Section("rtsp").Key("m3u8_dir_path").MustString("")
	tsDurationSecond := utils.Conf().Section("rtsp").Key("ts_duration_second").MustInt(6)
	SaveStreamToLocal := false
	if (len(ffmpeg) > 0) && localRecord > 0 && len(m3u8DirPath) > 0 {
		err = utils.EnsureDir(m3u8DirPath)
		if err != nil {
			log.Error(fmt.Sprintf("Create m3u8_dir_path[%s] err:%v.", m3u8DirPath, err))
		} else {
			SaveStreamToLocal = true
		}
	}
	go func() { // save to local.
		pusher2ffmpegMap := make(map[*Pusher]*exec.Cmd)
		if SaveStreamToLocal {
			log.Info("Prepare to save stream to local....")
			defer log.Info("End save stream to local....")
		}
		var pusher *Pusher
		addChnOk := true
		removeChnOk := true
		for addChnOk || removeChnOk {
			select {
			case pusher, addChnOk = <-server.addPusherCh:
				if SaveStreamToLocal {
					if addChnOk {
						dir := path.Join(m3u8DirPath, pusher.Path(), time.Now().Format("20060102"))
						err := utils.EnsureDir(dir)
						if err != nil {
							log.Error(fmt.Sprintf("EnsureDir:[%s] err:%v.", dir, err))
							continue
						}
						m3u8path := path.Join(dir, fmt.Sprintf("out.m3u8"))
						port := pusher.Server().TCPPort
						rtsp := fmt.Sprintf("rtsp://localhost:%d%s", port, pusher.Path())
						paramStr := utils.Conf().Section("rtsp").Key(pusher.Path()).MustString("-c:v copy -c:a aac")
						params := []string{"-fflags", "genpts", "-rtsp_transport", "tcp", "-i", rtsp, "-hls_time", strconv.Itoa(tsDurationSecond), "-hls_list_size", "0", m3u8path}
						if paramStr != "default" {
							paramsOfThisPath := strings.Split(paramStr, " ")
							params = append(params[:6], append(paramsOfThisPath, params[6:]...)...)
						}
						// ffmpeg -i ~/Downloads/720p.mp4 -s 640x360 -g 15 -c:a aac -hls_time 5 -hls_list_size 0 record.m3u8
						cmd := exec.Command(ffmpeg, params...)
						f, err := os.OpenFile(path.Join(dir, fmt.Sprintf("log.txt")), os.O_RDWR|os.O_CREATE, 0755)
						if err == nil {
							cmd.Stdout = f
							cmd.Stderr = f
						}
						err = cmd.Start()
						if err != nil {
							log.Error(fmt.Sprintf("Start ffmpeg err:%v", err))
						}
						pusher2ffmpegMap[pusher] = cmd
						log.Info(fmt.Sprintf("add ffmpeg [%v] to pull stream from pusher[%v]", cmd, pusher))
					} else {
						log.Info("addPusherChan closed")
					}
				}
			case pusher, removeChnOk = <-server.removePusherCh:
				if SaveStreamToLocal {
					if removeChnOk {
						cmd := pusher2ffmpegMap[pusher]
						proc := cmd.Process
						if proc != nil {
							log.Info(fmt.Sprintf("prepare to SIGTERM to process:%v", proc))
							proc.Signal(syscall.SIGTERM)
							proc.Wait()
							// proc.Kill()
							// no need to close attached log file.
							// see "Wait releases any resources associated with the Cmd."
							// if closer, ok := cmd.Stdout.(io.Closer); ok {
							// 	closer.Close()
							// 	logger.Printf("process:%v Stdout closed.", proc)
							// }
							log.Info(fmt.Sprintf("process:%v terminate.", proc))
						}
						delete(pusher2ffmpegMap, pusher)
						log.Info(fmt.Sprintf("delete ffmpeg from pull stream from pusher[%v]", pusher))
					} else {
						for _, cmd := range pusher2ffmpegMap {
							proc := cmd.Process
							if proc != nil {
								log.Info(fmt.Sprintf("prepare to SIGTERM to process:%v", proc))
								proc.Signal(syscall.SIGTERM)
							}
						}
						pusher2ffmpegMap = make(map[*Pusher]*exec.Cmd)
						log.Info("removePusherChan closed")
					}
				}
			}
		}
	}()

	server.Stopped = false
	server.TCPListener = listener
	log.Info("rtsp server start on: ", server.TCPPort)
	networkBuffer := utils.Conf().Section("rtsp").Key("network_buffer").MustInt(1048576)
	for !server.Stopped {
		var (
			conn net.Conn
		)
		if conn, err = server.TCPListener.Accept(); err != nil {
			log.Error(err)
			continue
		}
		if tcpConn, ok := conn.(*net.TCPConn); ok {
			if err = tcpConn.SetReadBuffer(networkBuffer); err != nil {
				log.Error(fmt.Sprintf("rtsp server conn set read buffer error, %v", err))
			}
			if err = tcpConn.SetWriteBuffer(networkBuffer); err != nil {
				log.Error(fmt.Sprintf("rtsp server conn set write buffer error, %v", err))
			}
		}

		session := NewSession(server, conn)
		go session.Start()
	}
	return
}

func (server *Server) Stop() {
	log.Info("rtsp server stop on: ", server.TCPPort)
	server.Stopped = true
	if server.TCPListener != nil {
		server.TCPListener.Close()
		server.TCPListener = nil
	}
	server.pushersLock.Lock()
	server.pushers = make(map[string]*Pusher)
	server.pushersLock.Unlock()

	close(server.addPusherCh)
	close(server.removePusherCh)
}

func (server *Server) AddPusher(pusher *Pusher) bool {
	added := false
	server.pushersLock.Lock()
	_, ok := server.pushers[pusher.Path()]
	if !ok {
		server.pushers[pusher.Path()] = pusher
		log.Info(fmt.Sprintf("%v start, now pusher size[%d]", pusher, len(server.pushers)))
		added = true
	} else {
		added = false
	}
	server.pushersLock.Unlock()
	if added {
		go pusher.Start()
		server.addPusherCh <- pusher
	}
	return added
}

func (server *Server) TryAttachToPusher(session *Session) (int, *Pusher) {
	server.pushersLock.Lock()
	attached := 0
	var pusher *Pusher = nil
	if _pusher, ok := server.pushers[session.Path]; ok {
		if _pusher.RebindSession(session) {
			log.Info("Attached to a pusher")
			attached = 1
			pusher = _pusher
		} else {
			attached = -1
		}
	}
	server.pushersLock.Unlock()
	return attached, pusher
}

func (server *Server) RemovePusher(pusher *Pusher) {
	removed := false
	server.pushersLock.Lock()
	if _pusher, ok := server.pushers[pusher.Path()]; ok && pusher.ID() == _pusher.ID() {
		delete(server.pushers, pusher.Path())
		log.Info(fmt.Sprintf("%v end, now pusher size[%d]\n", pusher, len(server.pushers)))
		removed = true
	}
	server.pushersLock.Unlock()
	if removed {
		server.removePusherCh <- pusher
	}
}

func (server *Server) GetPusher(path string) (pusher *Pusher) {
	server.pushersLock.RLock()
	pusher = server.pushers[path]
	server.pushersLock.RUnlock()
	return
}

func (server *Server) GetPushers() (pushers map[string]*Pusher) {
	pushers = make(map[string]*Pusher)
	server.pushersLock.RLock()
	for k, v := range server.pushers {
		pushers[k] = v
	}
	server.pushersLock.RUnlock()
	return
}

func (server *Server) GetPusherSize() (size int) {
	server.pushersLock.RLock()
	size = len(server.pushers)
	server.pushersLock.RUnlock()
	return
}

package rtsp

import (
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/MeloQi/EasyGoLib/utils"
	"net"
	"sync"
)

type Server struct {
	TCPListener   *net.TCPListener
	TCPPort       int
	Stopped       bool
	pushers       map[string]*Pusher // Path <-> Pusher
	pushersLock   sync.RWMutex
	recorders     map[string]*Recorder // id <-> Recorder
	recordersLock sync.RWMutex
}

var instance *Server = nil

func GetServer() *Server {
	if instance == nil {
		instance = &Server{
			Stopped:   true,
			TCPPort:   utils.Conf().Section("rtsp").Key("port").MustInt(554),
			pushers:   make(map[string]*Pusher),
			recorders: make(map[string]*Recorder),
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
	server.pushersLock.Lock()
	if _pusher, ok := server.pushers[pusher.Path()]; ok && pusher.ID() == _pusher.ID() {
		delete(server.pushers, pusher.Path())
		log.Info(fmt.Sprintf("%v end, now pusher size[%d]\n", pusher, len(server.pushers)))
	}
	server.pushersLock.Unlock()
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

func (server *Server) RemoveRecorder(r *Recorder) {
	server.recordersLock.Lock()
	if _, ok := server.recorders[r.Id]; ok {
		delete(server.recorders, r.Id)
		log.Info(fmt.Sprintf("%v end, current recorders: [%d]\n", r, len(server.recorders)))
	}
	server.recordersLock.Unlock()
}

func (server *Server) AddRecorder(r *Recorder) bool {
	added := false
	server.recordersLock.Lock()
	if _, ok := server.recorders[r.Id]; !ok {
		server.recorders[r.Id] = r
		log.Info(fmt.Sprintf("%v added, current recorders: [%d]", r, len(server.recorders)))
		added = true
	} else {
		added = false
	}
	server.recordersLock.Unlock()
	if added {
		r.Start()
	}
	return added
}

func (server *Server) GetRecorders() (rs map[string]*Recorder) {
	rs = make(map[string]*Recorder)
	server.recordersLock.RLock()
	for k, v := range server.recorders {
		rs[k] = v
	}
	server.recordersLock.RUnlock()
	return
}

func (server *Server) GetRecorder(id string) (recorder *Recorder) {
	server.recordersLock.RLock()
	recorder = server.recorders[id]
	server.recordersLock.RUnlock()
	return
}

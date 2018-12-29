package rtsp

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/penggy/EasyGoLib/utils"

	"github.com/teris-io/shortid"
)

type RTPPack struct {
	Type   RTPType
	Buffer *bytes.Buffer
}

type SessionType int

const (
	SESSION_TYPE_PUSHER SessionType = iota
	SESSEION_TYPE_PLAYER
)

func (st SessionType) String() string {
	switch st {
	case SESSION_TYPE_PUSHER:
		return "pusher"
	case SESSEION_TYPE_PLAYER:
		return "player"
	}
	return "unknow"
}

type RTPType int

const (
	RTP_TYPE_AUDIO RTPType = iota
	RTP_TYPE_VIDEO
	RTP_TYPE_AUDIOCONTROL
	RTP_TYPE_VIDEOCONTROL
)

func (rt RTPType) String() string {
	switch rt {
	case RTP_TYPE_AUDIO:
		return "audio"
	case RTP_TYPE_VIDEO:
		return "video"
	case RTP_TYPE_AUDIOCONTROL:
		return "audio control"
	case RTP_TYPE_VIDEOCONTROL:
		return "video control"
	}
	return "unknow"
}

type TransType int

const (
	TRANS_TYPE_TCP TransType = iota
	TRANS_TYPE_UDP
)

func (tt TransType) String() string {
	switch tt {
	case TRANS_TYPE_TCP:
		return "TCP"
	case TRANS_TYPE_UDP:
		return "UDP"
	}
	return "unknow"
}

const UDP_BUF_SIZE = 1048576

type Session struct {
	SessionLogger
	ID        string
	Server    *Server
	Conn      *RichConn
	connRW    *bufio.ReadWriter
	connWLock sync.RWMutex
	Type      SessionType
	TransType TransType
	Path      string
	URL       string
	SDPRaw    string
	SDPMap    map[string]*SDPInfo

	AControl string
	VControl string
	ACodec   string
	VCodec   string

	// stats info
	InBytes  int
	OutBytes int
	StartAt  time.Time
	Timeout  int

	Stoped bool

	//tcp channels
	aRTPChannel        int
	aRTPControlChannel int
	vRTPChannel        int
	vRTPControlChannel int

	Pusher      *Pusher
	Player      *Player
	UDPClient   *UDPClient
	RTPHandles  []func(*RTPPack)
	StopHandles []func()
}

func (session *Session) String() string {
	return fmt.Sprintf("session[%v][%v][%s][%s]", session.Type, session.TransType, session.Path, session.ID)
}

func NewSession(server *Server, conn net.Conn) *Session {
	networkBuffer := utils.Conf().Section("rtsp").Key("network_buffer").MustInt(204800)
	timeoutMillis := utils.Conf().Section("rtsp").Key("timeout").MustInt(0)
	timeoutTCPConn := &RichConn{conn, time.Duration(timeoutMillis) * time.Millisecond}

	session := &Session{
		ID:      shortid.MustGenerate(),
		Server:  server,
		Conn:    timeoutTCPConn,
		connRW:  bufio.NewReadWriter(bufio.NewReaderSize(timeoutTCPConn, networkBuffer), bufio.NewWriterSize(timeoutTCPConn, networkBuffer)),
		StartAt: time.Now(),
		Timeout: utils.Conf().Section("rtsp").Key("timeout").MustInt(0),

		RTPHandles:  make([]func(*RTPPack), 0),
		StopHandles: make([]func(), 0),
	}

	session.logger = log.New(os.Stdout, fmt.Sprintf("[%s]", session.ID), log.LstdFlags|log.Lshortfile)
	if !utils.Debug {
		session.logger.SetOutput(utils.GetLogWriter())
	}
	return session
}

func (session *Session) Stop() {
	if session.Stoped {
		return
	}
	session.Stoped = true
	for _, h := range session.StopHandles {
		h()
	}
	if session.Conn != nil {
		session.connRW.Flush()
		session.Conn.Close()
		session.Conn = nil
	}
	if session.UDPClient != nil {
		session.UDPClient.Stop()
		session.UDPClient = nil
	}
}

func (session *Session) Start() {
	defer session.Stop()
	buf1 := make([]byte, 1)
	buf2 := make([]byte, 2)
	logger := session.logger
	for !session.Stoped {
		if _, err := io.ReadFull(session.connRW, buf1); err != nil {
			logger.Println(session, err)
			return
		}
		if buf1[0] == 0x24 { //rtp data
			if _, err := io.ReadFull(session.connRW, buf1); err != nil {
				logger.Println(err)
				return
			}
			if _, err := io.ReadFull(session.connRW, buf2); err != nil {
				logger.Println(err)
				return
			}
			channel := int(buf1[0])
			rtpLen := int(binary.BigEndian.Uint16(buf2))
			rtpBytes := make([]byte, rtpLen)
			if _, err := io.ReadFull(session.connRW, rtpBytes); err != nil {
				logger.Println(err)
				return
			}
			rtpBuf := bytes.NewBuffer(rtpBytes)
			var pack *RTPPack
			switch channel {
			case session.aRTPChannel:
				pack = &RTPPack{
					Type:   RTP_TYPE_AUDIO,
					Buffer: rtpBuf,
				}
			case session.aRTPControlChannel:
				pack = &RTPPack{
					Type:   RTP_TYPE_AUDIOCONTROL,
					Buffer: rtpBuf,
				}
			case session.vRTPChannel:
				pack = &RTPPack{
					Type:   RTP_TYPE_VIDEO,
					Buffer: rtpBuf,
				}
			case session.vRTPControlChannel:
				pack = &RTPPack{
					Type:   RTP_TYPE_VIDEOCONTROL,
					Buffer: rtpBuf,
				}
			default:
				logger.Printf("unknow rtp pack type, %v", pack.Type)
				continue
			}
			if pack == nil {
				logger.Printf("session tcp got nil rtp pack")
				continue
			}
			session.InBytes += rtpLen + 4
			for _, h := range session.RTPHandles {
				h(pack)
			}
		} else { // rtsp cmd
			reqBuf := bytes.NewBuffer(nil)
			reqBuf.Write(buf1)
			for !session.Stoped {
				if line, isPrefix, err := session.connRW.ReadLine(); err != nil {
					logger.Println(err)
					return
				} else {
					reqBuf.Write(line)
					if !isPrefix {
						reqBuf.WriteString("\r\n")
					}
					if len(line) == 0 {
						req := NewRequest(reqBuf.String())
						if req == nil {
							break
						}
						session.InBytes += reqBuf.Len()
						contentLen := req.GetContentLength()
						session.InBytes += contentLen
						if contentLen > 0 {
							bodyBuf := make([]byte, contentLen)
							if n, err := io.ReadFull(session.connRW, bodyBuf); err != nil {
								logger.Println(err)
								return
							} else if n != contentLen {
								logger.Printf("read rtsp request body failed, expect size[%d], got size[%d]", contentLen, n)
								return
							}
							req.Body = string(bodyBuf)
						}
						session.handleRequest(req)
						break
					}
				}
			}
		}
	}
}

func (session *Session) handleRequest(req *Request) {
	//if session.Timeout > 0 {
	//	session.Conn.SetDeadline(time.Now().Add(time.Duration(session.Timeout) * time.Second))
	//}
	logger := session.logger
	logger.Printf("<<<\n%s", req)
	res := NewResponse(200, "OK", req.Header["CSeq"], session.ID, "")
	defer func() {
		if p := recover(); p != nil {
			res.StatusCode = 500
			res.Status = fmt.Sprintf("Inner Server Error, %v", p)
		}
		logger.Printf(">>>\n%s", res)
		outBytes := []byte(res.String())
		session.connWLock.Lock()
		session.connRW.Write(outBytes)
		session.connRW.Flush()
		session.connWLock.Unlock()
		session.OutBytes += len(outBytes)
		switch req.Method {
		case "PLAY", "RECORD":
			switch session.Type {
			case SESSEION_TYPE_PLAYER:
				session.Pusher.AddPlayer(session.Player)
			case SESSION_TYPE_PUSHER:
				session.Server.AddPusher(session.Pusher)
			}
		case "TEARDOWN":
			{
				session.Stop()
				return
			}
		}
		if res.StatusCode != 200 {
			logger.Printf("Response request error[%d]. stop session.", res.StatusCode)
			session.Stop()
		}
	}()
	switch req.Method {
	case "OPTIONS":
		res.Header["Public"] = "DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, OPTIONS, ANNOUNCE, RECORD"
	case "ANNOUNCE":
		session.Type = SESSION_TYPE_PUSHER
		session.URL = req.URL

		url, err := url.Parse(req.URL)
		if err != nil {
			res.StatusCode = 500
			res.Status = "Invalid URL"
			return
		}
		session.Path = url.Path

		session.SDPRaw = req.Body
		session.SDPMap = ParseSDP(req.Body)
		sdp, ok := session.SDPMap["audio"]
		if ok {
			session.AControl = sdp.Control
			session.ACodec = sdp.Codec
			logger.Printf("audio codec[%s]\n", session.ACodec)
		}
		sdp, ok = session.SDPMap["video"]
		if ok {
			session.VControl = sdp.Control
			session.VCodec = sdp.Codec
			logger.Printf("video codec[%s]\n", session.VCodec)
		}
		session.Pusher = NewPusher(session)
		if session.Server.GetPusher(session.Path) == nil {
			session.Server.AddPusher(session.Pusher)
		} else {
			res.StatusCode = 406
			res.Status = "Not Acceptable"
			return
		}
	case "DESCRIBE":
		session.Type = SESSEION_TYPE_PLAYER
		session.URL = req.URL

		url, err := url.Parse(req.URL)
		if err != nil {
			res.StatusCode = 500
			res.Status = "Invalid URL"
			return
		}
		session.Path = url.Path
		pusher := session.Server.GetPusher(session.Path)
		if pusher == nil {
			res.StatusCode = 404
			res.Status = "NOT FOUND"
			return
		}
		session.Player = NewPlayer(session, pusher)
		session.Pusher = pusher
		session.AControl = pusher.AControl()
		session.VControl = pusher.VControl()
		session.ACodec = pusher.ACodec()
		session.VCodec = pusher.VCodec()
		session.Conn.timeout = 0
		res.SetBody(session.Pusher.SDPRaw())
	case "SETUP":
		ts := req.Header["Transport"]
		// control字段可能是`stream=1`字样，也可能是rtsp://...字样。这里只要control字段与session的url合并起来，等于SETUP的URL，那就视为相同的stream
		// 例1：
		// a=control:streamid=1
		// 例2：
		// a=control:rtsp://192.168.1.64/trackID=1
		setupUrl, err := url.Parse(req.URL)
		if err != nil {
			res.StatusCode = 500
			res.Status = "Invalid URL"
			return
		}
		if setupUrl.Port() == "" {
			setupUrl.Host = fmt.Sprintf("%s:%s", setupUrl.Host, "554")
		}
		setupURL := setupUrl.String()
		vURL := session.VControl
		if strings.Index(strings.TrimSpace(strings.ToLower(vURL)), "rtsp://") != 0 {
			vURL = strings.TrimRight(session.URL, "/") + "/" + strings.TrimLeft(session.VControl, "/")
		}
		aURL := session.AControl
		if strings.Index(strings.TrimSpace(strings.ToLower(aURL)), "rtsp://") != 0 {
			aURL = strings.TrimRight(session.URL, "/") + "/" + strings.TrimLeft(session.AControl, "/")
		}
		mtcp := regexp.MustCompile("interleaved=(\\d+)(-(\\d+))?")
		mudp := regexp.MustCompile("client_port=(\\d+)(-(\\d+))?")

		if tcpMatchs := mtcp.FindStringSubmatch(ts); tcpMatchs != nil {
			session.TransType = TRANS_TYPE_TCP
			if setupURL == aURL {
				session.aRTPChannel, _ = strconv.Atoi(tcpMatchs[1])
				session.aRTPControlChannel, _ = strconv.Atoi(tcpMatchs[3])
			} else if setupURL == vURL {
				session.vRTPChannel, _ = strconv.Atoi(tcpMatchs[1])
				session.vRTPControlChannel, _ = strconv.Atoi(tcpMatchs[3])
			}
			logger.Printf("Parse SETUP req.TRANSPORT:TCP.Session.Type:%d,control:%s, AControl:%s,VControl:%s", session.Type, setupURL, aURL, vURL)
		} else if udpMatchs := mudp.FindStringSubmatch(ts); udpMatchs != nil {
			session.TransType = TRANS_TYPE_UDP
			// no need for tcp timeout.
			session.Conn.timeout = 0
			logger.Printf("Parse SETUP req.TRANSPORT:UDP.Session.Type:%d,control:%s, AControl:%s,VControl:%s", session.Type, setupURL, aURL, vURL)
			if session.UDPClient == nil {
				session.UDPClient = &UDPClient{
					Session: session,
				}
			}
			if session.Type == SESSION_TYPE_PUSHER && session.Pusher.UDPServer == nil {
				session.Pusher.UDPServer = &UDPServer{
					Session: session,
				}
			}
			if setupURL == aURL {
				session.UDPClient.APort, _ = strconv.Atoi(udpMatchs[1])
				session.UDPClient.AControlPort, _ = strconv.Atoi(udpMatchs[3])
				if err := session.UDPClient.SetupAudio(); err != nil {
					res.StatusCode = 500
					res.Status = fmt.Sprintf("udp client setup audio error, %v", err)
					return
				}

				if session.Type == SESSION_TYPE_PUSHER {
					if err := session.Pusher.UDPServer.SetupAudio(); err != nil {
						res.StatusCode = 500
						res.Status = fmt.Sprintf("udp server setup audio error, %v", err)
						return
					}
					tss := strings.Split(ts, ";")
					idx := -1
					for i, val := range tss {
						if val == udpMatchs[0] {
							idx = i
						}
					}
					tail := append([]string{}, tss[idx+1:]...)
					tss = append(tss[:idx+1], fmt.Sprintf("server_port=%d-%d", session.Pusher.UDPServer.APort, session.Pusher.UDPServer.AControlPort))
					tss = append(tss, tail...)
					ts = strings.Join(tss, ";")
				}
			} else if setupURL == vURL {
				session.UDPClient.VPort, _ = strconv.Atoi(udpMatchs[1])
				session.UDPClient.VControlPort, _ = strconv.Atoi(udpMatchs[3])
				if err := session.UDPClient.SetupVideo(); err != nil {
					res.StatusCode = 500
					res.Status = fmt.Sprintf("udp client setup video error, %v", err)
					return
				}

				if session.Type == SESSION_TYPE_PUSHER {
					if err := session.Pusher.UDPServer.SetupVideo(); err != nil {
						res.StatusCode = 500
						res.Status = fmt.Sprintf("udp server setup video error, %v", err)
						return
					}
					tss := strings.Split(ts, ";")
					idx := -1
					for i, val := range tss {
						if val == udpMatchs[0] {
							idx = i
						}
					}
					tail := append([]string{}, tss[idx+1:]...)
					tss = append(tss[:idx+1], fmt.Sprintf("server_port=%d-%d", session.Pusher.UDPServer.VPort, session.Pusher.UDPServer.VControlPort))
					tss = append(tss, tail...)
					ts = strings.Join(tss, ";")
				}
			} else {
				logger.Printf("SETUP got UnKown control:%s", setupURL)
			}
		}
		res.Header["Transport"] = ts
	case "PLAY":
		res.Header["Range"] = req.Header["Range"]
	case "RECORD":
	}
}

func (session *Session) SendRTP(pack *RTPPack) (err error) {
	if pack == nil {
		err = fmt.Errorf("player send rtp got nil pack")
		return
	}
	if session.TransType == TRANS_TYPE_UDP {
		if session.UDPClient == nil {
			err = fmt.Errorf("player use udp transport but udp client not found")
			return
		}
		err = session.UDPClient.SendRTP(pack)
		return
	}
	switch pack.Type {
	case RTP_TYPE_AUDIO:
		bufChannel := make([]byte, 2)
		bufChannel[0] = 0x24
		bufChannel[1] = byte(session.aRTPChannel)
		session.connWLock.Lock()
		session.connRW.Write(bufChannel)
		bufLen := make([]byte, 2)
		binary.BigEndian.PutUint16(bufLen, uint16(pack.Buffer.Len()))
		session.connRW.Write(bufLen)
		session.connRW.Write(pack.Buffer.Bytes())
		session.connRW.Flush()
		session.connWLock.Unlock()
		session.OutBytes += pack.Buffer.Len() + 4
	case RTP_TYPE_AUDIOCONTROL:
		bufChannel := make([]byte, 2)
		bufChannel[0] = 0x24
		bufChannel[1] = byte(session.aRTPControlChannel)
		session.connWLock.Lock()
		session.connRW.Write(bufChannel)
		bufLen := make([]byte, 2)
		binary.BigEndian.PutUint16(bufLen, uint16(pack.Buffer.Len()))
		session.connRW.Write(bufLen)
		session.connRW.Write(pack.Buffer.Bytes())
		session.connRW.Flush()
		session.connWLock.Unlock()
		session.OutBytes += pack.Buffer.Len() + 4
	case RTP_TYPE_VIDEO:
		bufChannel := make([]byte, 2)
		bufChannel[0] = 0x24
		bufChannel[1] = byte(session.vRTPChannel)
		session.connWLock.Lock()
		session.connRW.Write(bufChannel)
		bufLen := make([]byte, 2)
		binary.BigEndian.PutUint16(bufLen, uint16(pack.Buffer.Len()))
		session.connRW.Write(bufLen)
		session.connRW.Write(pack.Buffer.Bytes())
		session.connRW.Flush()
		session.connWLock.Unlock()
		session.OutBytes += pack.Buffer.Len() + 4
	case RTP_TYPE_VIDEOCONTROL:
		bufChannel := make([]byte, 2)
		bufChannel[0] = 0x24
		bufChannel[1] = byte(session.vRTPControlChannel)
		session.connWLock.Lock()
		session.connRW.Write(bufChannel)
		bufLen := make([]byte, 2)
		binary.BigEndian.PutUint16(bufLen, uint16(pack.Buffer.Len()))
		session.connRW.Write(bufLen)
		session.connRW.Write(pack.Buffer.Bytes())
		session.connRW.Flush()
		session.connWLock.Unlock()
		session.OutBytes += pack.Buffer.Len() + 4
	default:
		err = fmt.Errorf("session tcp send rtp got unkown pack type[%v]", pack.Type)
	}
	return
}

package rtsp

import (
	"fmt"
	"net"
	"strings"

	"github.com/MeloQi/EasyGoLib/utils"
)

type UDPClient struct {
	*Session
	APort        int
	AConn        *net.UDPConn
	AControlPort int
	AControlConn *net.UDPConn
	VPort        int
	VConn        *net.UDPConn
	VControlPort int
	VControlConn *net.UDPConn
	Stopped      bool
}

func (s *UDPClient) Stop() {
	if s.Stopped {
		return
	}
	s.Stopped = true
	if s.AConn != nil {
		s.AConn.Close()
		s.AConn = nil
	}
	if s.AControlConn != nil {
		s.AControlConn.Close()
		s.AControlConn = nil
	}
	if s.VConn != nil {
		s.VConn.Close()
		s.VConn = nil
	}
	if s.VControlConn != nil {
		s.VControlConn.Close()
		s.VControlConn = nil
	}
}

func (s *UDPClient) SetupAudio() (err error) {
	var (
		logger = s.logger
		addr   *net.UDPAddr
	)
	defer func() {
		if err != nil {
			logger.Println(err)
			s.Stop()
		}
	}()
	host := s.Conn.RemoteAddr().String()
	host = host[:strings.LastIndex(host, ":")]
	if addr, err = net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", host, s.APort)); err != nil {
		return
	}
	s.AConn, err = net.DialUDP("udp", nil, addr)
	if err != nil {
		return
	}
	networkBuffer := utils.Conf().Section("rtsp").Key("network_buffer").MustInt(1048576)
	if err = s.AConn.SetReadBuffer(networkBuffer); err != nil {
		logger.Printf("udp client audio conn set read buffer error, %v", err)
	}
	if err = s.AConn.SetWriteBuffer(networkBuffer); err != nil {
		logger.Printf("udp client audio conn set write buffer error, %v", err)
	}

	addr, err = net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", host, s.AControlPort))
	if err != nil {
		return
	}
	s.AControlConn, err = net.DialUDP("udp", nil, addr)
	if err != nil {
		return
	}
	if err = s.AControlConn.SetReadBuffer(networkBuffer); err != nil {
		logger.Printf("udp client audio control conn set read buffer error, %v", err)
	}
	if err = s.AControlConn.SetWriteBuffer(networkBuffer); err != nil {
		logger.Printf("udp client audio control conn set write buffer error, %v", err)
	}
	return
}

func (s *UDPClient) SetupVideo() (err error) {
	var (
		logger = s.logger
		addr   *net.UDPAddr
	)
	defer func() {
		if err != nil {
			logger.Println(err)
			s.Stop()
		}
	}()
	host := s.Conn.RemoteAddr().String()
	host = host[:strings.LastIndex(host, ":")]
	if addr, err = net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", host, s.VPort)); err != nil {
		return
	}
	if s.VConn, err = net.DialUDP("udp", nil, addr); err != nil {
		return
	}
	networkBuffer := utils.Conf().Section("rtsp").Key("network_buffer").MustInt(1048576)
	if err = s.VConn.SetReadBuffer(networkBuffer); err != nil {
		logger.Printf("udp client video conn set read buffer error, %v", err)
	}
	if err = s.VConn.SetWriteBuffer(networkBuffer); err != nil {
		logger.Printf("udp client video conn set write buffer error, %v", err)
	}

	addr, err = net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", host, s.VControlPort))
	if err != nil {
		return
	}
	s.VControlConn, err = net.DialUDP("udp", nil, addr)
	if err != nil {
		return
	}
	if err = s.VControlConn.SetReadBuffer(networkBuffer); err != nil {
		logger.Printf("udp client video control conn set read buffer error, %v", err)
	}
	if err = s.VControlConn.SetWriteBuffer(networkBuffer); err != nil {
		logger.Printf("udp client video control conn set write buffer error, %v", err)
	}
	return
}

func (s *UDPClient) SendRTP(pack *RTPPack) (err error) {
	if pack == nil {
		err = fmt.Errorf("udp client send rtp got nil pack")
		return
	}
	var conn *net.UDPConn
	switch pack.Type {
	case RtpTypeAudio:
		conn = s.AConn
	case RtpTypeAudioControl:
		conn = s.AControlConn
	case RtpTypeVideo:
		conn = s.VConn
	case RtpTypeVideoControl:
		conn = s.VControlConn
	default:
		err = fmt.Errorf("udp client send rtp got unkown pack type[%v]", pack.Type)
		return
	}
	if conn == nil {
		err = fmt.Errorf("udp client send rtp pack type[%v] failed, conn not found", pack.Type)
		return
	}
	var n int
	if n, err = conn.Write(pack.Buffer.Bytes()); err != nil {
		err = fmt.Errorf("udp client write bytes error, %v", err)
		return
	}
	// logger.Printf("udp client write [%d/%d]", n, pack.Buffer.Len())
	s.Session.OutBytes += n
	return
}

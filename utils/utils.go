package utils

import (
	"errors"
	"fmt"
	"github.com/MeloQi/EasyGoLib/utils"
	"net"
	"os/exec"
)

func GetFullAddress(addr string) string {
	if len(addr) > 0 {
		ret := addr
		if ':' == addr[0] {
			ret = fmt.Sprintf("localhost%s", ret)
		}
		return ret
	}

	return ""
}

func CommandExists(cmd string) bool {
	_, err := exec.LookPath(cmd)
	return err == nil
}

func ExternalIP() (string, error) {
	ifaces, err := net.Interfaces()
	if err != nil {
		return "", err
	}
	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 {
			continue // interface down
		}
		if iface.Flags&net.FlagLoopback != 0 {
			continue // loopback interface
		}
		addrs, err := iface.Addrs()
		if err != nil {
			return "", err
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.IsLoopback() {
				continue
			}
			ip = ip.To4()
			if ip == nil {
				continue // not an ipv4 address
			}
			return ip.String(), nil
		}
	}
	return "", errors.New("are you connected to the network?")
}

func GetHostName() string {
	port := utils.Conf().Section("http").Key("port").MustInt(10008)
	host, err := ExternalIP()
	if err != nil {
		host = "localhost"
	}
	defaultHost := fmt.Sprintf("http://%s:%d", host, port)
	return utils.Conf().Section("http").Key("hostname").MustString(defaultHost)
}

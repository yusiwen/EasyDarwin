package utils

import (
	"fmt"
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

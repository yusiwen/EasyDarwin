package utils

import (
	"fmt"
	"testing"
)

func TestExternalIP(t *testing.T) {
	host, err := ExternalIP()
	if err != nil {
		t.Error(err)
	}
	fmt.Println(host)
}

package rtsp

import (
	"fmt"
	"testing"
	"time"

	"github.com/teris-io/shortid"
)

func TestRecorder(t *testing.T) {
	id := shortid.MustGenerate()
	handler := func(r *Recorder) {
		fmt.Println("recorder stopped")
	}
	r := NewRecorder(id, "rtsp://lattepanda:18554/sample01", fmt.Sprintf("d:\\tmp\\output-%s.mp4", id), handler)
	err := r.Start()
	if err != nil {
		fmt.Println(err)
	}
	err = r.Start()
	if err != nil {
		fmt.Println(err)
	}
	time.Sleep(10 * time.Second)
	r.Stop()

	<-r.stopped
	fmt.Println(r.running)
}

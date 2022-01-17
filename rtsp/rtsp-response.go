package rtsp

import (
	"fmt"
	"strconv"
)

type Response struct {
	Version    string
	StatusCode int
	Status     string
	Header     map[string]interface{}
	Body       string
}

func NewResponse(statusCode int, status, cSeq, sid, body string) *Response {
	res := &Response{
		Version:    RtspVersion,
		StatusCode: statusCode,
		Status:     status,
		Header:     map[string]interface{}{"CSeq": cSeq, "Session": sid},
		Body:       body,
	}
	bodyLen := len(body)
	if bodyLen > 0 {
		res.Header["Content-Length"] = strconv.Itoa(bodyLen)
	} else {
		delete(res.Header, "Content-Length")
	}
	return res
}

func (r *Response) String() string {
	str := fmt.Sprintf("%s %d %s\r\n", r.Version, r.StatusCode, r.Status)
	for key, value := range r.Header {
		str += fmt.Sprintf("%s: %s\r\n", key, value)
	}
	str += "\r\n"
	str += r.Body
	return str
}

func (r *Response) SetBody(body string) {
	bodyLen := len(body)
	r.Body = body
	if bodyLen > 0 {
		r.Header["Content-Length"] = strconv.Itoa(bodyLen)
	} else {
		delete(r.Header, "Content-Length")
	}
}

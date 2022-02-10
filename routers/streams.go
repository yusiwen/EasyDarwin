package routers

import (
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/EasyDarwin/EasyDarwin/extension"
	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/EasyDarwin/EasyDarwin/models"
	"github.com/EasyDarwin/EasyDarwin/rtsp"
	"github.com/MeloQi/EasyGoLib/db"
	"github.com/gin-gonic/gin"
	"github.com/teris-io/shortid"
)

/**
 * @apiDefine stream 流管理
 */

/**
 * @api {get} /api/v1/stream/start 启动拉转推
 * @apiGroup stream
 * @apiName StreamStart
 * @apiParam {String} url RTSP源地址
 * @apiParam {String} [customPath] 转推时的推送PATH
 * @apiParam {String=TCP,UDP} [transType=TCP] 拉流传输模式
 * @apiParam {Number} [idleTimeout] 拉流时的超时时间
 * @apiParam {Number} [heartbeatInterval] 拉流时的心跳间隔，毫秒为单位。如果心跳间隔不为0，那拉流时会向源地址以该间隔发送OPTION请求用来心跳保活
 * @apiSuccess (200) {String} ID	拉流的ID。后续可以通过该ID来停止拉流
 */
func (h *APIHandler) StreamStart(c *gin.Context) {
	type Form struct {
		URL               string `form:"url" binding:"required"`
		CustomPath        string `form:"customPath"`
		TransType         string `form:"transType"`
		IdleTimeout       int    `form:"idleTimeout"`
		HeartbeatInterval int    `form:"heartbeatInterval"`
	}
	var form Form
	err := c.Bind(&form)
	if err != nil {
		log.Error("pull to push err: ", err)
		return
	}
	agent := fmt.Sprintf("EasyDarwinGo/%s", BuildVersion)
	if BuildDateTime != "" {
		agent = fmt.Sprintf("%s(%s)", agent, BuildDateTime)
	}
	id := shortid.MustGenerate()
	client, err := rtsp.NewRTSPClient(id, rtsp.GetServer(), form.URL, int64(form.HeartbeatInterval)*1000, agent)
	if err != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest, err.Error())
		return
	}
	if form.CustomPath != "" && !strings.HasPrefix(form.CustomPath, "/") {
		form.CustomPath = "/" + form.CustomPath
	}
	client.CustomPath = form.CustomPath
	switch strings.ToLower(form.TransType) {
	case "udp":
		client.TransType = rtsp.TransTypeUdp
	case "tcp":
		fallthrough
	default:
		client.TransType = rtsp.TransTypeTcp
	}

	flvPusher := extension.NewFlvPusher(client.URL, client.ACodec, client.VCodec, extension.GetFlvServer(),
		"live", client.ID)
	pusher := rtsp.NewClientPusher(client)
	pusher.FlvPusher = flvPusher

	if rtsp.GetServer().GetPusher(pusher.Path()) != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Path %s already exists", client.Path))
		return
	}
	err = client.Start(time.Duration(form.IdleTimeout) * time.Second)
	if err != nil {
		log.Error("pull stream err: ", err)
		c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Pull stream err: %v", err))
		return
	}
	log.Info("pull to push %v success ", form)
	rtsp.GetServer().AddPusher(pusher)
	// save to db.
	var stream = models.Stream{
		ID:                id,
		URL:               form.URL,
		CustomPath:        form.CustomPath,
		IdleTimeout:       form.IdleTimeout,
		HeartbeatInterval: form.HeartbeatInterval,
		Status:            models.Running,
	}
	result := db.SQL.Create(&stream)
	if result.Error != nil {
		log.Warn(result.Error)
	}

	c.IndentedJSON(200, pusher.ID())
}

/**
 * @api {get} /api/v1/stream/stop 停止推流
 * @apiGroup stream
 * @apiName StreamStop
 * @apiParam {String} id 拉流的ID
 * @apiUse simpleSuccess
 */
func (h *APIHandler) StreamStopOrStart(c *gin.Context) {
	type Form struct {
		ID   string `form:"id" binding:"required"`
		Flag string `form:"flag" binding:"required"`
	}
	var form Form
	err := c.Bind(&form)
	if err != nil {
		log.Error("stop pull to push err: ", err)
		return
	}
	pushers := rtsp.GetServer().GetPushers()
	for _, v := range pushers {
		if v.ID() == form.ID {
			v.Stop()
			c.IndentedJSON(200, "OK")
			log.Info(fmt.Sprintf("Stop %v success ", v))
			if v.RTSPClient != nil {
				var stream models.Stream
				stream.ID = form.ID
				stream.URL = v.RTSPClient.URL
				if "0" == form.Flag {
					stream.Status = models.Stopped
				} else {
					stream.Status = models.Running
				}
				db.SQL.Model(&stream).Update("status", stream.Status)
			}
			return
		}
	}
	c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Pusher[%s] not found", form.ID))
}

/**
 * @api {get} /api/v1/stream/stop 停止推流
 * @apiGroup stream
 * @apiName StreamStop
 * @apiParam {String} id 拉流的ID
 * @apiUse simpleSuccess
 */
func (h *APIHandler) StreamDelete(c *gin.Context) {
	type Form struct {
		ID string `form:"id" binding:"required"`
	}
	var form Form
	err := c.Bind(&form)
	if err != nil {
		log.Error("stop pull to push err: ", err)
		return
	}
	pushers := rtsp.GetServer().GetPushers()
	for _, v := range pushers {
		if v.ID() == form.ID {
			v.Stop()
			c.IndentedJSON(200, "OK")
			log.Info(fmt.Sprintf("Stop %v success ", v))
			if v.RTSPClient != nil {
				var stream models.Stream
				stream.URL = v.RTSPClient.URL
				db.SQL.Delete(stream, "id = ?", form.ID)
			}
			return
		}
	}
	c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Pusher[%s] not found", form.ID))
}

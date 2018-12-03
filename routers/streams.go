package routers

import (
	"fmt"
	"log"
	"net/http"
	"strings"
	"time"

	"github.com/EasyDarwin/EasyDarwin/rtsp"
	"github.com/gin-gonic/gin"
)

/**
 * @apiDefine stream 流管理
 */

/**
 * @api {get} /api/vi/stream/start 启动拉流
 * @apiGroup stream
 * @apiName StreamStart
 * @apiParam {String} url RTSP源地址
 * @apiParam {String} [customPath] 转推时的推送PATH
 * @apiParam {Number} [idleTimeout] 拉流时的超时时间
 * @apiParam {Number} [heartbeatInterval] 拉流时的心跳间隔，毫秒为单位。如果心跳间隔不为0，那拉流时会向源地址以该间隔发送OPTION请求用来心跳保活
 * @apiSuccess (200) {String} ID	拉流的ID。后续可以通过该ID来停止拉流
 */
func (h *APIHandler) StreamStart(c *gin.Context) {
	type Form struct {
		URL               string `form:"url" binding:"required"`
		CustomPath        string `form:"customPath"`
		IdleTimeout       int    `form:"idleTimeout"`
		HeartbeatInterval int    `form:"heartbeatInterval"`
	}
	var form Form
	err := c.Bind(&form)
	if err != nil {
		log.Printf("Pull to push err:%v", err)
		return
	}
	client, err := rtsp.NewRTSPClient(rtsp.GetServer(), form.URL, int64(form.HeartbeatInterval)*1000)
	if err != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest, err.Error())
		return
	}
	if form.CustomPath != "" && !strings.HasPrefix(form.CustomPath, "/") {
		form.CustomPath = "/" + form.CustomPath
	}
	client.CustomPath = form.CustomPath

	pusher := rtsp.NewClientPusher(client)
	if rtsp.GetServer().GetPusher(pusher.Path()) != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Path %s already exists", client.Path))
		return
	}
	err = client.Start(time.Duration(form.IdleTimeout) * time.Second)
	if err != nil {
		log.Printf("Pull stream err :%v", err)
		c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Pull stream err: %v", err))
		return
	}
	log.Printf("Pull to push %v success ", form)
	rtsp.GetServer().AddPusher(pusher)
	c.IndentedJSON(200, pusher.ID())
}

/**
 * @api {get} /api/vi/stream/stop 停止拉流
 * @apiGroup stream
 * @apiName StreamStop
 * @apiParam {String} id 拉流的ID
 * @apiUse simpleSuccess
 */
func (h *APIHandler) StreamStop(c *gin.Context) {
	type Form struct {
		ID string `form:"id" binding:"required"`
	}
	var form Form
	err := c.Bind(&form)
	if err != nil {
		log.Printf("stop pull to push err:%v", err)
		return
	}
	pushers := rtsp.GetServer().GetPushers()
	for _, v := range pushers {
		if v.ID() == form.ID {
			v.Stop()
			c.IndentedJSON(200, "OK")
			log.Printf("Stop %v success ", v)
			return
		}
	}
	c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("Pusher[%s] not found", form.ID))
}

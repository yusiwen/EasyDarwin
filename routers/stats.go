package routers

import (
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/rtsp"
	"github.com/MeloQi/EasyGoLib/utils"
	"github.com/gin-gonic/gin"
	"strings"
)

/**
 * @apiDefine stats 统计
 */

/**
 * @apiDefine playerInfo
 * @apiSuccess (200) {String} rows.id
 * @apiSuccess (200) {String} rows.path
 * @apiSuccess (200) {String} rows.transType 传输模式
 * @apiSuccess (200) {Number} rows.inBytes 入口流量
 * @apiSuccess (200) {Number} rows.outBytes 出口流量
 * @apiSuccess (200) {String} rows.startAt 开始时间
 */

/**
 * @api {get} /api/v1/pushers 获取推流列表
 * @apiGroup stats
 * @apiName Pushers
 * @apiParam {Number} [start] 分页开始,从零开始
 * @apiParam {Number} [limit] 分页大小
 * @apiParam {String} [sort] 排序字段
 * @apiParam {String=ascending,descending} [order] 排序顺序
 * @apiParam {String} [q] 查询参数
 * @apiSuccess (200) {Number} total 总数
 * @apiSuccess (200) {Array} rows 推流列表
 * @apiSuccess (200) {String} rows.id
 * @apiSuccess (200) {String} rows.path
 * @apiSuccess (200) {String} rows.transType 传输模式
 * @apiSuccess (200) {Number} rows.inBytes 入口流量
 * @apiSuccess (200) {Number} rows.outBytes 出口流量
 * @apiSuccess (200) {String} rows.startAt 开始时间
 * @apiSuccess (200) {Number} rows.onlines 在线人数
 */
func (h *APIHandler) Pushers(c *gin.Context) {
	form := utils.NewPageForm()
	if err := c.Bind(form); err != nil {
		return
	}
	hostname := utils.GetRequestHostname(c.Request)
	pushers := make([]interface{}, 0)
	for _, pusher := range rtsp.GetServer().GetPushers() {
		port := pusher.Server().TCPPort
		rtspUrl := []string{fmt.Sprintf("rtsp://%s:%d%s", hostname, port, pusher.Path())}

		flvPusher := pusher.FlvPusher
		if flvPusher != nil {
			flvPortStr := utils.Conf().Section("flv").Key("flv_addr").MustString(":7001")
			if len(flvPortStr) > 0 && strings.Contains(flvPortStr, ":") {
				flvPortStr = strings.Split(flvPortStr, ":")[1]
			}
			flvPath := fmt.Sprintf("/%s/%s.flv", flvPusher.AppName, flvPusher.RoomName)
			rtspUrl = append(rtspUrl, fmt.Sprintf("http://%s:%s%s", hostname, flvPortStr, flvPath))
		}
		if form.Q != "" && !strings.Contains(strings.ToLower(rtspUrl[0]), strings.ToLower(form.Q)) {
			continue
		}
		pushers = append(pushers, map[string]interface{}{
			"id":        pusher.ID(),
			"url":       rtspUrl,
			"path":      pusher.Path(),
			"source":    pusher.Source(),
			"transType": pusher.TransType(),
			"inBytes":   pusher.InBytes(),
			"outBytes":  pusher.OutBytes(),
			"startAt":   utils.DateTime(pusher.StartAt()),
			"onlines":   len(pusher.GetPlayers()),
		})
	}
	pr := utils.NewPageResult(pushers)
	if form.Sort != "" {
		pr.Sort(form.Sort, form.Order)
	}
	pr.Slice(form.Start, form.Limit)
	c.IndentedJSON(200, pr)
}

/**
 * @api {get} /api/v1/players 获取拉流列表
 * @apiGroup stats
 * @apiName Players
 * @apiParam {Number} [start] 分页开始,从零开始
 * @apiParam {Number} [limit] 分页大小
 * @apiParam {String} [sort] 排序字段
 * @apiParam {String=ascending,descending} [order] 排序顺序
 * @apiParam {String} [q] 查询参数
 * @apiSuccess (200) {Number} total 总数
 * @apiSuccess (200) {Array} rows 推流列表
 * @apiSuccess (200) {String} rows.id
 * @apiSuccess (200) {String} rows.path
 * @apiSuccess (200) {String} rows.transType 传输模式
 * @apiSuccess (200) {Number} rows.inBytes 入口流量
 * @apiSuccess (200) {Number} rows.outBytes 出口流量
 * @apiSuccess (200) {String} rows.startAt 开始时间
 */
func (h *APIHandler) Players(c *gin.Context) {
	form := utils.NewPageForm()
	if err := c.Bind(form); err != nil {
		return
	}
	players := make([]*rtsp.Player, 0)
	for _, pusher := range rtsp.GetServer().GetPushers() {
		for _, player := range pusher.GetPlayers() {
			players = append(players, player)
		}
	}
	hostname := utils.GetRequestHostname(c.Request)
	_players := make([]interface{}, 0)
	for i := 0; i < len(players); i++ {
		player := players[i]
		port := player.Server.TCPPort
		rtspUrl := fmt.Sprintf("rtsp://%s:%d%s", hostname, port, player.Path)
		if port == 554 {
			rtspUrl = fmt.Sprintf("rtsp://%s%s", hostname, player.Path)
		}
		_players = append(_players, map[string]interface{}{
			"id":        player.ID,
			"path":      rtspUrl,
			"transType": player.TransType.String(),
			"inBytes":   player.InBytes,
			"outBytes":  player.OutBytes,
			"startAt":   utils.DateTime(player.StartAt),
		})
	}
	pr := utils.NewPageResult(_players)
	if form.Sort != "" {
		pr.Sort(form.Sort, form.Order)
	}
	pr.Slice(form.Start, form.Limit)
	c.IndentedJSON(200, pr)
}

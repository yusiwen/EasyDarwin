package routers

import (
	"bytes"
	"fmt"
	"github.com/EasyDarwin/EasyDarwin/models"
	"github.com/EasyDarwin/EasyDarwin/rtsp"
	"github.com/MeloQi/EasyGoLib/db"
	"github.com/teris-io/shortid"
	"math"
	"net/http"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/MeloQi/EasyGoLib/utils"
	"github.com/gin-gonic/gin"
)

/**
 * @apiDefine record 录像
 */

/**
 * @apiDefine fileInfo
 * @apiSuccess (200) {String} duration	格式化好的录像时长
 * @apiSuccess (200) {Number} durationMillis	录像时长，毫秒为单位
 * @apiSuccess (200) {String} path 录像文件的相对路径,其绝对路径为：http[s]://host:port/record/[path]。
 * @apiSuccess (200) {String} folder 录像文件夹，录像文件夹以推流路径命名。
 */

/**
 * @api {get} /api/v1/record/folders 获取所有录像文件夹
 * @apiGroup record
 * @apiName RecordFolders
 * @apiParam {Number} [start] 分页开始,从零开始
 * @apiParam {Number} [limit] 分页大小
 * @apiParam {String} [sort] 排序字段
 * @apiParam {String=ascending,descending} [order] 排序顺序
 * @apiParam {String} [q] 查询参数
 * @apiSuccess (200) {Number} total 总数
 * @apiSuccess (200) {Array} rows 文件夹列表
 * @apiSuccess (200) {String} rows.folder	录像文件夹名称
 */
func (h *APIHandler) RecordFolders(c *gin.Context) {
	mp4Path := utils.Conf().Section("rtsp").Key("m3u8_dir_path").MustString("")
	form := utils.NewPageForm()
	if err := c.Bind(form); err != nil {
		log.Error("record folder bind err: ", err)
		return
	}
	var files = make([]interface{}, 0)
	if mp4Path != "" {
		visit := func(files *[]interface{}) filepath.WalkFunc {
			return func(path string, info os.FileInfo, err error) error {
				if err != nil {
					return err
				}
				if path == mp4Path {
					return nil
				}
				if !info.IsDir() {
					return nil
				}
				*files = append(*files, map[string]interface{}{"folder": info.Name()})
				return filepath.SkipDir
			}
		}
		err := filepath.Walk(mp4Path, visit(&files))
		if err != nil {
			log.Error("Query RecordFolders err: ", err)
		}
	}
	pr := utils.NewPageResult(files)
	if form.Sort != "" {
		pr.Sort(form.Sort, form.Order)
	}
	pr.Slice(form.Start, form.Limit)
	c.IndentedJSON(200, pr)

}

/**
 * @api {get} /api/v1/record/files 获取所有录像文件
 * @apiGroup record
 * @apiName RecordFiles
 * @apiParam {Number} folder 录像文件所在的文件夹
 * @apiParam {Number} [start] 分页开始,从零开始
 * @apiParam {Number} [limit] 分页大小
 * @apiParam {String} [sort] 排序字段
 * @apiParam {String=ascending,descending} [order] 排序顺序
 * @apiParam {String} [q] 查询参数
 * @apiSuccess (200) {Number} total 总数
 * @apiSuccess (200) {Array} rows 文件列表
 * @apiSuccess (200) {String} rows.duration	格式化好的录像时长
 * @apiSuccess (200) {Number} rows.durationMillis	录像时长，毫秒为单位
 * @apiSuccess (200) {String} rows.path 录像文件的相对路径,录像文件为m3u8格式，将其放到video标签中便可直接播放。其绝对路径为：http[s]://host:port/record/[path]。
 */
func (h *APIHandler) RecordFiles(c *gin.Context) {
	type Form struct {
		utils.PageForm
		Folder  string `form:"folder" binding:"required"`
		StartAt int    `form:"beginUTCSecond"`
		StopAt  int    `form:"endUTCSecond"`
	}
	var form = Form{}
	form.Limit = math.MaxUint32
	err := c.Bind(&form)
	if err != nil {
		log.Error("record file bind err: ", err)
		return
	}

	files := make([]interface{}, 0)
	mp4Path := utils.Conf().Section("rtsp").Key("m3u8_dir_path").MustString("")
	if mp4Path != "" {
		ffmpegPath := utils.Conf().Section("rtsp").Key("ffmpeg_path").MustString("")
		ffmpegFolder, executable := filepath.Split(ffmpegPath)
		split := strings.Split(executable, ".")
		suffix := ""
		if len(split) > 1 {
			suffix = split[1]
		}
		ffprobe := ffmpegFolder + "ffprobe" + suffix
		folder := filepath.Join(mp4Path, form.Folder)
		visit := func(files *[]interface{}) filepath.WalkFunc {
			return func(path string, info os.FileInfo, err error) error {
				if err != nil {
					return err
				}
				if path == folder {
					return nil
				}
				if info.IsDir() {
					return nil
				}
				if info.Size() == 0 {
					return nil
				}
				if info.Name() == ".DS_Store" {
					return nil
				}
				if !strings.HasSuffix(strings.ToLower(info.Name()), ".m3u8") && !strings.HasSuffix(strings.ToLower(info.Name()), ".ts") {
					return nil
				}
				cmd := exec.Command(ffprobe, "-i", path)
				cmdOutput := &bytes.Buffer{}
				//cmd.Stdout = cmdOutput
				cmd.Stderr = cmdOutput
				err = cmd.Run()
				outputBytes := cmdOutput.Bytes()
				output := string(outputBytes)
				//log.Printf("%v result:%v", cmd, output)
				var average = regexp.MustCompile(`Duration: ((\d+):(\d+):(\d+).(\d+))`)
				result := average.FindStringSubmatch(output)
				duration := time.Duration(0)
				durationStr := ""
				if len(result) > 0 {
					durationStr = result[1]
					h, _ := strconv.Atoi(result[2])
					duration += time.Duration(h) * time.Hour
					m, _ := strconv.Atoi(result[3])
					duration += time.Duration(m) * time.Minute
					s, _ := strconv.Atoi(result[4])
					duration += time.Duration(s) * time.Second
					millis, _ := strconv.Atoi(result[5])
					duration += time.Duration(millis) * time.Millisecond
				}

				*files = append(*files, map[string]interface{}{
					"path":           path[len(mp4Path):],
					"durationMillis": duration / time.Millisecond,
					"duration":       durationStr})
				return nil
			}
		}
		err = filepath.Walk(folder, visit(&files))
		if err != nil {
			log.Error("Query RecordFolders err: ", err)
		}
	}

	pr := utils.NewPageResult(files)
	if form.Sort != "" {
		pr.Sort(form.Sort, form.Order)
	}
	pr.Slice(form.Start, form.Limit)
	c.IndentedJSON(200, pr)
}

/**
 * @api {get} /api/v1/record/start 开始录制
 * @apiGroup record
 * @apiName RecordStart
 * @apiParam {String} streamId 流ID
 * @apiParam {String} tag Tag
 * @apiSuccess (200) {String} file 视频文件地址
 */
func (h *APIHandler) RecordStart(c *gin.Context) {
	type Form struct {
		StreamId string `form:"streamId" binding:"required"`
		Tag      string `form:"tag" binding:"required"`
	}
	var form = Form{}
	err := c.Bind(&form)
	if err != nil {
		log.Error("record bind err: ", err)
		c.IndentedJSON(http.StatusBadRequest, "request error")
		return
	}

	var stream models.Stream
	result := db.SQL.First(&stream, "id = ?", form.StreamId)
	if result.Error != nil {
		log.Error("stream not found", result.Error)
		c.IndentedJSON(526, "Stream not found")
		return
	}

	p := utils.Conf().Section("record").Key("output_dir_path").MustString(utils.CWD())
	p = path.Join(p, form.Tag)
	err = os.MkdirAll(p, os.ModePerm)
	if err != nil {
		log.Error("cannot create directory: ", p, err)
		c.IndentedJSON(526, "File system error")
		return
	}
	t := utils.Conf().Section("record").Key("output_file_format").MustString("m3u8")
	format := rtsp.M3u8
	if "mp4" == t {
		format = rtsp.Mp4
	}
	f := path.Join(p, fmt.Sprintf("%s.%s", form.StreamId, format))

	var count int64 = 0
	db.SQL.Model(&models.Record{}).Where("stream_id = ? AND tag = ?", form.StreamId, form.Tag).Count(&count)
	if count > 0 {
		log.Error("record already exists")
		c.IndentedJSON(526, "Record already exists")
		return
	}

	recorderId := shortid.MustGenerate()
	result = db.SQL.Create(&models.Record{
		Id:        recorderId,
		StreamId:  form.StreamId,
		Tag:       form.Tag,
		Output:    f,
		StartTime: time.Now(),
	})
	if result.Error != nil {
		log.Error("record db insert failed: ", result.Error)
		c.IndentedJSON(526, "Record create failed")
		return
	}

	stopHandler := func(r *rtsp.Recorder) {
		db.SQL.Model(&models.Record{}).Where("id = ?", r.Id).Update("end_time", time.Now())
		rtsp.GetServer().RemoveRecorder(r)
	}
	recorder := rtsp.NewRecorder(recorderId, stream.URL, f, stopHandler)
	recorder.OutputFormat = format
	rtsp.GetServer().AddRecorder(recorder)
	c.IndentedJSON(200, f)
}

/**
 * @api {get} /api/v1/record/stop 结束录制
 * @apiGroup record
 * @apiName RecordStop
 * @apiParam {String} streamId 流ID
 * @apiParam {String} tag Tag
 * @apiSuccess (200) {String} file 视频文件地址
 */
func (h *APIHandler) RecordStop(c *gin.Context) {
	type Form struct {
		StreamId string `form:"streamId" binding:"required"`
		Tag      string `form:"tag" binding:"required"`
	}
	var form = Form{}
	err := c.Bind(&form)
	if err != nil {
		log.Error("record bind err: ", err)
		c.IndentedJSON(http.StatusBadRequest, "request error")
		return
	}

	var record models.Record
	result := db.SQL.Where("stream_id = ? AND tag = ?", form.StreamId, form.Tag).First(&record)
	if result.Error != nil {
		log.Error("record not found", result.Error)
		c.IndentedJSON(526, "Record not found")
		return
	}

	r := rtsp.GetServer().GetRecorder(record.Id)
	if r == nil {
		log.Error("record not found: ", record.Id)
		c.IndentedJSON(526, "Record not found")
		return
	}

	r.Stop()
	c.IndentedJSON(200, r.File)
}

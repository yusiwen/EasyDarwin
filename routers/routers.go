package routers

import (
	"fmt"
	"mime"
	"net/http"
	"strings"

	"github.com/EasyDarwin/EasyDarwin/log"
	"github.com/MeloQi/EasyGoLib/db"
	"github.com/MeloQi/EasyGoLib/utils"
	"github.com/MeloQi/sessions"
	assetfs "github.com/elazarl/go-bindata-assetfs"
	"github.com/gin-contrib/pprof"
	"github.com/gin-contrib/static"
	"github.com/gin-gonic/gin"
	"github.com/penggy/cors"
	"gopkg.in/go-playground/validator.v8"
)

/**
 * @apiDefine simpleSuccess
 * @apiSuccessExample 成功
 * HTTP/1.1 200 OK
 */

/**
 * @apiDefine authError
 * @apiErrorExample 认证失败
 * HTTP/1.1 401 access denied
 */

/**
 * @apiDefine pageParam
 * @apiParam {Number} start 分页开始,从零开始
 * @apiParam {Number} limit 分页大小
 * @apiParam {String} [sort] 排序字段
 * @apiParam {String=ascending,descending} [order] 排序顺序
 * @apiParam {String} [q] 查询参数
 */

/**
 * @apiDefine pageSuccess
 * @apiSuccess (200) {Number} total 总数
 * @apiSuccess (200) {Array} rows 分页数据
 */

/**
 * @apiDefine timeInfo
 * @apiSuccess (200) {String} rows.createAt 创建时间, YYYY-MM-DD HH:mm:ss
 * @apiSuccess (200) {String} rows.updateAt 结束时间, YYYY-MM-DD HH:mm:ss
 */

var Router *gin.Engine

func init() {
	mime.AddExtensionType(".svg", "image/svg+xml")
	mime.AddExtensionType(".m3u8", "application/vnd.apple.mpegurl")
	// mime.AddExtensionType(".m3u8", "application/x-mpegurl")
	mime.AddExtensionType(".ts", "video/mp2t")
	// prevent on Windows with Dreamware installed, modified registry .css -> application/x-css
	// see https://stackoverflow.com/questions/22839278/python-built-in-server-not-loading-css
	mime.AddExtensionType(".css", "text/css; charset=utf-8")

	gin.DisableConsoleColor()
	gin.SetMode(gin.ReleaseMode)
	gin.DefaultWriter = utils.GetLogWriter()
}

func Errors() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Next()
		for _, err := range c.Errors {
			switch err.Type {
			case gin.ErrorTypeBind:
				switch err.Err.(type) {
				case validator.ValidationErrors:
					errs := err.Err.(validator.ValidationErrors)
					for _, err := range errs {
						sec := utils.Conf().Section("localize")
						field := sec.Key(err.Field).MustString(err.Field)
						tag := sec.Key(err.Tag).MustString(err.Tag)
						c.AbortWithStatusJSON(http.StatusBadRequest, fmt.Sprintf("%s %s", field, tag))
						return
					}
				default:
					log.Error(err.Err.Error())
					c.AbortWithStatusJSON(http.StatusBadRequest, "Inner Error")
					return
				}
			}
		}
	}
}

func NeedLogin() gin.HandlerFunc {
	return func(c *gin.Context) {
		if sessions.Default(c).Get("uid") == nil {
			c.AbortWithStatusJSON(http.StatusUnauthorized, "Unauthorized")
			return
		}
		c.Next()
	}
}

type binaryFileSystem struct {
	fs http.FileSystem
}

func (b *binaryFileSystem) Open(name string) (http.File, error) {
	return b.fs.Open(name)
}

func (b *binaryFileSystem) Exists(prefix string, filepath string) bool {

	if p := strings.TrimPrefix(filepath, prefix); len(p) < len(filepath) {
		if _, err := b.fs.Open(p); err != nil {
			return false
		}
		return true
	}
	return false
}

func Init(assetFS *assetfs.AssetFS) (err error) {
	Router = gin.New()
	pprof.Register(Router)
	// Router.Use(gin.Logger())
	Router.Use(gin.Recovery())
	Router.Use(Errors())
	Router.Use(cors.Default())

	store := sessions.NewGormStoreWithOptions(db.SQL, sessions.GormStoreOptions{}, []byte("EasyDarwin@2018"))
	tokenTimeout := utils.Conf().Section("http").Key("token_timeout").MustInt(7 * 86400)
	store.Options(sessions.Options{HttpOnly: true, MaxAge: tokenTimeout, Path: "/"})
	sessionHandle := sessions.Sessions("token", store)

	{
		fs := binaryFileSystem{
			assetFS,
		}
		Router.Use(static.Serve("/", &fs))
	}

	{
		api := Router.Group("/api/v1").Use(sessionHandle)
		api.GET("/login", API.Login)
		api.GET("/userinfo", API.UserInfo)
		api.GET("/logout", API.Logout)
		api.GET("/defaultlogininfo", API.DefaultLoginInfo)
		api.GET("/modifypassword", NeedLogin(), API.ModifyPassword)
		api.GET("/serverinfo", API.GetServerInfo)
		api.GET("/restart", API.Restart)

		api.GET("/pushers", API.Pushers)
		api.GET("/players", API.Players)

		api.GET("/stream/start", API.StreamStart)
		api.GET("/stream/toggle", API.StreamToggle)
		api.GET("/stream/delete", API.StreamDelete)

		api.GET("/record/folders", API.RecordFolders)
		api.GET("/record/files", API.RecordFiles)
		api.GET("/record/start", API.RecordStart)
		api.GET("/record/stop", API.RecordStop)
		api.GET("/record/screenshot", API.Screenshot)
	}

	{
		mp4Path := utils.Conf().Section("record").Key("output_dir_path").MustString("")
		if len(mp4Path) != 0 {
			Router.Use(static.Serve("/records", static.LocalFile(mp4Path, false)))
		}
	}

	return
}

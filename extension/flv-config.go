package extension

import (
	"bytes"
	"encoding/json"
	"github.com/gwuhaolin/livego/configure"
	"github.com/kr/pretty"
	log "github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"strings"
)

type Application struct {
	AppName    string   `json:"appname"`
	Live       bool     `json:"live"`
	Hls        bool     `json:"hls"`
	Flv        bool     `json:"flv"`
	Api        bool     `json:"api"`
	StaticPush []string `json:"static_push"`
}

type Applications []Application

type JWT struct {
	Secret    string `json:"secret"`
	Algorithm string `json:"algorithm"`
}

type ServerCfg struct {
	Level           string       `json:"level"`
	ConfigFile      string       `json:"config_file"`
	FLVArchive      bool         `json:"flv_archive"`
	FLVDir          string       `json:"flv_dir"`
	RTMPNoAuth      bool         `json:"rtmp_noauth"`
	RTMPAddr        string       `json:"rtmp_addr"`
	HTTPFLVAddr     string       `json:"httpflv_addr"`
	HLSAddr         string       `json:"hls_addr"`
	HLSKeepAfterEnd bool         `json:"hls_keep_after_end"`
	APIAddr         string       `json:"api_addr"`
	RedisAddr       string       `json:"redis_addr"`
	RedisPwd        string       `json:"redis_pwd"`
	ReadTimeout     int          `json:"read_timeout"`
	WriteTimeout    int          `json:"write_timeout"`
	GopNum          int          `json:"gop_num"`
	JWT             JWT          `json:"jwt"`
	Server          Applications `json:"server"`
}

// default config
var defaultConf = ServerCfg{
	ConfigFile:      "livego.yaml",
	FLVArchive:      false,
	RTMPNoAuth:      false,
	RTMPAddr:        ":1935",
	HTTPFLVAddr:     ":7001",
	HLSAddr:         ":7002",
	HLSKeepAfterEnd: false,
	APIAddr:         ":8090",
	WriteTimeout:    10,
	ReadTimeout:     10,
	GopNum:          1,
	Server: Applications{{
		AppName:    "live",
		Live:       true,
		Hls:        true,
		Flv:        true,
		Api:        true,
		StaticPush: nil,
	}},
}

func init() {
	// Default config
	b, _ := json.Marshal(defaultConf)
	defaultConfig := bytes.NewReader(b)
	viper.SetConfigType("json")
	viper.ReadConfig(defaultConfig)
	configure.Config.MergeConfigMap(viper.AllSettings())

	// Environment
	replacer := strings.NewReplacer(".", "_")
	configure.Config.SetEnvKeyReplacer(replacer)
	configure.Config.AllowEmptyEnv(true)
	configure.Config.AutomaticEnv()

	// Log
	if l, err := log.ParseLevel(configure.Config.GetString("level")); err == nil {
		log.SetLevel(l)
		log.SetReportCaller(l == log.DebugLevel)
	}

	c := configure.ServerCfg{}
	configure.Config.Unmarshal(&c)
	log.Debugf("Current configurations: \n%# v", pretty.Formatter(c))
}

package models

import (
	"github.com/MeloQi/EasyGoLib/db"
	"github.com/MeloQi/EasyGoLib/utils"
	"strings"
)

func Init() (err error) {
	err = db.Init(&db.DBConfig{
		Type:     getDBType(utils.Conf().Section("db").Key("db_type").MustString("sqlite")),
		File:     utils.Conf().Section("db").Key("db_datafile").MustString(""),
		URI:      utils.Conf().Section("db").Key("db_uri").MustString(""),
		LogLevel: utils.Conf().Section("db").Key("db_log_level").MustString("silent"),
	})
	if err != nil {
		return
	}
	db.SQL.AutoMigrate(User{}, Stream{})
	count := int64(0)
	sec := utils.Conf().Section("http")
	defUser := sec.Key("default_username").MustString("admin")
	defPass := sec.Key("default_password").MustString("admin")
	db.SQL.Model(User{}).Where("username = ?", defUser).Count(&count)
	if count == 0 {
		db.SQL.Create(&User{
			Username: defUser,
			Password: utils.MD5(defPass),
		})
	}
	return
}

func getDBType(t string) db.DBType {
	st := strings.ToLower(t)
	switch st {
	case "mysql":
		return db.MySQL
	case "postgres":
		return db.Postgres
	default:
		return db.SQLite
	}

}

func Close() {
	db.Close()
}

package models

import (
	"github.com/MeloQi/EasyGoLib/db"
	"testing"
	"time"
)

func TestRecorder(t *testing.T) {
	err := db.Init(&db.DBConfig{
		Type:     db.MySQL,
		File:     "",
		URI:      "db_test_user:db_test_user_password@tcp(lattepanda:3306)/db_test?charset=utf8mb4&parseTime=True&loc=Local",
		LogLevel: "Info",
	})
	if err != nil {
		return
	}
	db.SQL.AutoMigrate(Record{})
	db.SQL.Create(&Record{
		Id:        "1234",
		StreamId:  "123456",
		Tag:       "12345",
		StartTime: time.Now(),
		EndTime:   nil,
	})
	return
}

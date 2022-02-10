package models

import "time"

type Record struct {
	Id        string     `gorm:"type:varchar(16);primary_key"`
	StreamId  string     `gorm:"type:varchar(16);uniqueIndex:idx_record"`
	Tag       string     `gorm:"type:varchar(256);uniqueIndex:idx_record"`
	Output    string     `gorm:"type:varchar(256)"`
	StartTime time.Time  `gorm:"type:datetime"`
	EndTime   *time.Time `gorm:"type:datetime"`
}

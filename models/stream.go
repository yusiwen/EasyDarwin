package models

type Stream struct {
	ID                string `gorm:"type:varchar(16);primary_key"`
	URL               string `gorm:"type:varchar(256);uniqueIndex"`
	CustomPath        string `gorm:"type:varchar(256)"`
	IdleTimeout       int
	HeartbeatInterval int
}

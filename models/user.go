package models

import (
	"github.com/MeloQi/EasyGoLib/utils"
	"gorm.io/gorm"
)

type User struct {
	ID       string `structs:"id" gorm:"primary_key;type:varchar(16);not null" form:"id" json:"id"`
	Username string `gorm:"type:varchar(64)"`
	Password string `gorm:"type:varchar(64)"`
	Role     string `gorm:"type:varchar(32)"`
	Reserve1 string `gorm:"type:varchar(256)"`
	Reserve2 string `gorm:"type:varchar(256)"`
}

func (user *User) BeforeCreate(tx *gorm.DB) error {
	user.ID = utils.ShortID()
	return nil
}

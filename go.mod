module github.com/EasyDarwin/EasyDarwin

go 1.17

require (
	github.com/MeloQi/EasyGoLib v0.0.0-20191209094052-803960583571
	github.com/MeloQi/service v0.0.0-20191030061151-7762127fe623
	github.com/MeloQi/sessions v0.0.0-20191030032128-1c51e5f867b9
	github.com/common-nighthawk/go-figure v0.0.0-20180619031829-18b2b544842c
	github.com/elazarl/go-bindata-assetfs v1.0.1
	github.com/gin-contrib/pprof v0.0.0-20180514151456-0ed7ffb6a189
	github.com/gin-contrib/static v0.0.0-20180301030858-73da7037e716
	github.com/gin-gonic/gin v1.3.0
	github.com/gwuhaolin/livego v0.0.0-20211209130643-e8d36e4519f0
	github.com/jinzhu/gorm v1.9.2-0.20180512062900-82eb9f8a5bbb
	github.com/kr/pretty v0.1.0
	github.com/penggy/cors v0.0.0-20180918145040-d08bb28f7e48
	github.com/pixelbender/go-sdp v0.0.0-20181123094152-100bc9371a0c
	github.com/shirou/gopsutil v2.17.13-0.20180417021151-63047d776e07+incompatible
	github.com/sirupsen/logrus v1.8.1
	github.com/spf13/viper v1.6.3
	github.com/t-tomalak/logrus-easy-formatter v0.0.0-20190827215021-c074f06c5816
	github.com/teris-io/shortid v0.0.0-20171029131806-771a37caa5cf
	github.com/u2takey/ffmpeg-go v0.4.0
	gopkg.in/go-playground/validator.v8 v8.18.2
)

require (
	github.com/StackExchange/wmi v0.0.0-20180412205111-cdffdb33acae // indirect
	github.com/auth0/go-jwt-middleware v0.0.0-20190805220309-36081240882b // indirect
	github.com/aws/aws-sdk-go v1.38.20 // indirect
	github.com/denisenkom/go-mssqldb v0.11.0 // indirect
	github.com/dgrijalva/jwt-go v3.2.0+incompatible // indirect
	github.com/eiannone/keyboard v0.0.0-20200508000154-caf4b762e807 // indirect
	github.com/erikstmartin/go-testdb v0.0.0-20160219214506-8d10e4a1bae5 // indirect
	github.com/fsnotify/fsnotify v1.4.9 // indirect
	github.com/gin-contrib/sse v0.0.0-20170109093832-22d885f9ecc7 // indirect
	github.com/go-ini/ini v1.66.2 // indirect
	github.com/go-ole/go-ole v1.2.2-0.20180213002836-a1ec82a652eb // indirect
	github.com/go-redis/redis v6.15.9+incompatible // indirect
	github.com/go-redis/redis/v7 v7.2.0 // indirect
	github.com/go-sql-driver/mysql v1.6.0 // indirect
	github.com/golang/protobuf v1.5.2 // indirect
	github.com/gorilla/context v1.1.1 // indirect
	github.com/gorilla/securecookie v1.1.1 // indirect
	github.com/gorilla/sessions v1.2.1 // indirect
	github.com/hashicorp/hcl v1.0.0 // indirect
	github.com/jinzhu/inflection v0.0.0-20180308033659-04140366298a // indirect
	github.com/jinzhu/now v1.1.4 // indirect
	github.com/jmespath/go-jmespath v0.4.0 // indirect
	github.com/jonboulle/clockwork v0.2.2 // indirect
	github.com/json-iterator/go v1.1.9 // indirect
	github.com/kr/text v0.1.0 // indirect
	github.com/lestrrat-go/file-rotatelogs v2.4.0+incompatible // indirect
	github.com/lestrrat-go/strftime v1.0.5 // indirect
	github.com/lib/pq v1.10.4 // indirect
	github.com/magiconair/properties v1.8.1 // indirect
	github.com/mattn/go-isatty v0.0.4 // indirect
	github.com/mattn/go-sqlite3 v1.14.10 // indirect
	github.com/mitchellh/mapstructure v1.1.2 // indirect
	github.com/modern-go/concurrent v0.0.0-20180306012644-bacd9c7ef1dd // indirect
	github.com/modern-go/reflect2 v1.0.1 // indirect
	github.com/onsi/ginkgo v1.16.5 // indirect
	github.com/onsi/gomega v1.17.0 // indirect
	github.com/patrickmn/go-cache v2.1.0+incompatible // indirect
	github.com/pelletier/go-toml v1.2.0 // indirect
	github.com/pkg/errors v0.9.1 // indirect
	github.com/satori/go.uuid v1.2.0 // indirect
	github.com/spf13/afero v1.2.2 // indirect
	github.com/spf13/cast v1.3.0 // indirect
	github.com/spf13/jwalterweatherman v1.0.0 // indirect
	github.com/spf13/pflag v1.0.5 // indirect
	github.com/stretchr/testify v1.7.0 // indirect
	github.com/subosito/gotenv v1.2.0 // indirect
	github.com/u2takey/go-utils v0.0.0-20200713025200-4704d09fc2c7 // indirect
	github.com/ugorji/go v0.0.0-20180307152341-02537d3a3e32 // indirect
	golang.org/x/net v0.0.0-20220111093109-d55c255bac03 // indirect
	golang.org/x/sys v0.0.0-20220111092808-5a964db01320 // indirect
	golang.org/x/text v0.3.6 // indirect
	google.golang.org/protobuf v1.26.0 // indirect
	gopkg.in/go-playground/assert.v1 v1.2.1 // indirect
	gopkg.in/ini.v1 v1.51.0 // indirect
	gopkg.in/yaml.v2 v2.4.0 // indirect
)

replace github.com/gwuhaolin/livego => github.com/yusiwen/livego v0.0.0-20220124072834-66782a230a25

replace github.com/u2takey/ffmpeg-go => github.com/yusiwen/ffmpeg-go v0.4.1-0.20220124090917-15bbd857e466

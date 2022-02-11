# EasyDarwin (mod version)

My custom modded version of EasyDarwin

## Change History

2022-01-05:

- Fix build by updating old invalid go modules
- Adjust project structures for web source
- Upgrade web source dependencies
- Adjust project build scripts
- Remove unused files
- Shrink git repo from 600MB to 60MB by removing large old history files
- Make single executable binary using go-bindata

2022-01-24:

- Integrate [my custom mod LiveGo](https://github.com/yusiwen/livego)
- Push rtmp stream to embedded livego server or external rtmp server

2022-01-27:

- Rewrite logger
- Enable settings for codecs for ffmpeg

2022-02-08:

- List flv links on web pages
- Support more databases other than SQLite

2022-02-10:

- Rewrite rtsp recording
- Add record APIs (RecordStart, RecordStop)

2022-02-11：

- Support record stream to mkv format
- Remove mp4 format due to corrupted file after cancelling ffmpeg process
- Add screenshot API
- Add hostname options to set hostname when deployed behind reverse proxy

## Build

### Pre-requisites

- Python 2.7
- Node.js 12.x
- Go 1.15+

Install building tools

```bash
# For front-end
$ npm install -g apidoc@0.29.0 rimraf

# For Go
$ go get github.com/akavel/rsrc
$ go get github.com/go-bindata/go-bindata/...
$ go get github.com/elazarl/go-bindata-assetfs/...
$ go get github.com/yusiwen/go-build-helper
```

### Install npm modules

```bash
$ npm install
```

### Start dev server for front-end development

```bash
$ npm run dev:www
```

### Development binary

Development builds are not packed with front-end files

```bash
$ npm run dev:win

# On linux
$ npm run dev:lin
```

### Build binary for Windows

Run command below in Git-bash or MinGW shell

```bash
$ npm run build:www
```

### Build binary for Linux

```bash
$ npm run build:lin
```

### Note on Running/Debug in Goland or LiteIDE/VSCode

- Copy `easydarwin-sample.ini` to `easydarwin.ini`, and change some options according to your environments.
- If embedded flv server is used, you must add the build flag: `-ldflags "-X 'github.com/gwuhaolin/livego/configure.BypassInit=true'"` to bypass LiveGo standalone initialization.
- Add `-config d:\\git\\EasyDarwin\\easydarwin.ini` to program arguments, change the file path according to your environment

Run configurations in GoLand should look like:

![image-20220209114302078](https://share.yusiwen.cn/public/pics/image-20220209114302078.png)

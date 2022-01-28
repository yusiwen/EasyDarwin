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

```bash
$ npm run build:www
```

### Build binary for Linux

```bash
$ npm run build:lin
```

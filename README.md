# EasyDarwin开源流媒体平台 #

**EasyDarwin**是由国内开源流媒体团队维护和迭代的一款开源流媒体视频平台框架，从2012年12月创建并发展至今，从原有单服务的开源流媒体服务器形式，拓展成现今的流媒体云平台架构的开源系列项目，更好地帮助广大流媒体开发者和创业型企业快速构建流媒体服务平台，更快、更简单地实现最新的移动互联网(安卓、iOS、H5、微信)流媒体直播与点播的需求，尤其是安防行业与互联网行业的衔接；

- 使用EasyDarwin收费吗？

	免费，EasyDarwin流媒体服务器开源版是在Apple开源项目Darwin Streaming Server 6.0.3的基础上进行开发和扩展的，遵循[Apple APSL](http://www.opensource.apple.com/license/apsl/ "Apple APSL")开源协议，EasyDarwin开源团队不会对开发者就代码使用上收取任何费用，我们只做开源流媒体的技术迭代开发和技术支持服务；

- EasyDarwin开源吗？

	EasyDarwin RTSP流媒体服务器完全开源，EasyDarwin RTSP流媒体服务器在Darwin Streaming Server基础上做的底层(Select/Epoll网络模型、无锁队列调度)和上层(RESTful接口、WEB管理、多平台编译)、关键帧索引（秒开画面）、远程运维等方面优化，这些都是全代码完全开源的；

	> EasyDarwin流媒体服务器后续将提供功能更加强大和完善的商用版本，支持点播、直播、RTMP、HLS、热升级、关键帧推送、秒开画面、远程运维、负载均衡等等功能；


## 云平台结构 ##

目前EasyDarwin流媒体平台整套解决方案包括有：**EasyCMS**(中心管理服务)，**EasyDarwin**(流媒体服务)，**EasyNVR**(通用摄像机接入服务)、**EasyCamera**(开源流媒体摄像机)、**EasyPlayer**（流媒体播放器）、**EasyClient**（开源云平台客户端）、以及周边众多工具库([**EasyHLS**](https://github.com/EasyDarwin/EasyHLS "EasyHLS") / [**EasyRTSPClient**](https://github.com/EasyDarwin/EasyRTSPClient "EasyRTSPClient") / [**EasyPusher**](https://github.com/EasyDarwin/EasyPusher "EasyPusher") / [**EasyAACEncoder**](https://github.com/EasyDarwin/EasyAACEncoder "EasyAACEncoder"))，后续也将继续扩展的录像、回放等多种服务和工具集，各个功能单元既可以独立使用于项目，又可以整体使用，形成一个完整、简单、易用、高效的流媒体解决方案：

1. **EasyCMS** 开源的设备接入与管理服务，支持多设备、多客户端接入，能非常快速地帮助大家实现稳定的设备接入服务，可以根据自己的需求进行服务功能拆分（例如用户接入服务与设备接入服务拆分等），具体见[https://github.com/EasyDarwin/EasyDarwin/tree/master/EasyCMS](https://github.com/EasyDarwin/EasyDarwin/tree/master/EasyCMS)；

1. **EasyDarwin** 核心流媒体服务！开源流媒体服务，高效、稳定、可靠、功能齐全，支持RTSP/HLS/HTTP流媒体协议，支持安防行业需要的摄像机流媒体转发功能、支持互联网行业需要的多平台(WEB、Android、IOS)点播（Mp4）、直播（H264/MJPEG/MPEG4、AAC/PCMA/PCMU/G726）功能，支持RESTful接口调用，具体接口调用方法和流程见：[https://github.com/EasyDarwin/EasyDarwin](https://github.com/EasyDarwin/EasyDarwin)；

1. **EasyCamera** 设备端（摄像机、移动设备、桌面程序）对接EasyDarwin平台的方案，跨平台，支持Windows、Linux、ARM，其中EasyDarwin摄像机是我们定制的一款摄像机硬件与EasyDarwin平台进行对接的方案，摄像机采用海思3518E方案，支持RTSP、Onvif、WEB管理、配套SDK工具，作为开发和演示硬件工具，我们提供了全套完备的程序和文档，既可以用于流媒体学习，又可以用于方案移植参考，更可以直接用于项目中，购买参考设备可以在：[https://easydarwin.taobao.com/](https://easydarwin.taobao.com/ "EasyDarwin")，用户可以将摄像机定制的部分替换成自己摄像机的硬件SDK，具体接入方法见[https://github.com/EasyDarwin/EasyCamera](https://github.com/EasyDarwin/EasyCamera)；

1. **EasyNVR** 摄像机（通用RTSP、Onvif摄像机）接入服务，EasyNVR能够通过简单的摄像机通道配置、存储配置、云平台对接配置、CDN配置等，将统监控行业里面的高清网络摄像机IP Camera、NVR、移动拍摄设备接入到EasyNVR，EasyNVR能够将这些视频源 的音视频数据采集到设备端，进行全平台终端直播、录像存储、录像检索和录像回放。并且EasyNVR能够将视频源的直播数据对接到第三方视频平台、CDN网络，实现互联网直播分发，具体接入方法见[https://github.com/EasyDarwin/EasyNVR](https://github.com/EasyDarwin/EasyNVR)；

1. **EasyClient** 是EasyDarwin开源流媒体云平台的客户端实现，包含：Windows、Android、iOS、H5(支持微信)四个部分，其主要功能包括：

	- 云平台设备列表获取；
	- 设备实时码流请求与播放；
	- 设备云台控制；
	- 设备语音对讲；


1. **EasyPlayer** RTSP流媒体播放客户端，目前只支持Windows桌面版本，后续将陆续支持Android、IOS版本，详细方案见[https://github.com/EasyDarwin/EasyPlayer](https://github.com/EasyDarwin/EasyPlayer)；


## 平台架构 ##
![](http://www.easydarwin.org/github/images/cloud_framework.png)

## 平台协议 ##

[![EasyDarwin Protocol](http://www.easydarwin.org/github/images/EProtocol.jpg)](https://github.com/EasyDarwin/EasyDarwin/tree/master/Doc "EasyDarwin Protocol")

## 默认端口 ##

- EasyCMS：接口服务端口,10000
- EasyDarwin：接口服务端口,10008
- EasyDarwin：RTSP服务端口,10554
- EasyDarwin：HTTP后台管理端口,10080
- EasyNVR：接口服务端口,10010
- EasyNVR：后台管理端口,10080


## 平台在线演示客户端 ##

- Windows版本：[https://github.com/EasyDarwin/EasyClient/releases](https://github.com/EasyDarwin/EasyClient/releases "EasyClient Windows")

- Android版本：[http://fir.im/EasyClient](http://fir.im/EasyClient "EasyClient Android")

![EasyClient Android](http://www.easydarwin.org/github/images/firimeasyclientandroid.png)

- iOS版本：[https://itunes.apple.com/us/app/easyclient/id1141850816](https://itunes.apple.com/us/app/easyclient/id1141850816 "EasyClient iOS")

![EasyClient iOS](http://www.easydarwin.org/github/images/firimeasyclientios.png)


## 系列视频教程 ##

EasyDarwin云平台编译、配置、部署教程：[http://edu.csdn.net/course/detail/2955](http://edu.csdn.net/course/detail/2955 "EasyDarwin")

EasyDarwin CSDN流媒体课堂：[http://edu.csdn.net/agency/index/213](http://edu.csdn.net/agency/index/213 "EasyDarwin")

## 商务合作 ##
#### 我们欢迎的合作模式： ####

越来越多的企业选择EasyDarwin开源平台作为他们产品和项目的基础，从而也产生了越来越多各种各样的需求，EasyDarwin开源团队接受与企业的项目合作：

- EasyDarwin平台现有项目的技术咨询与培训合作；
- EasyDarwin平台大方向上新项目/新技术的拓展合作；

#### 技术合作找Babosa@EasyDarwin.org： ####
- 邮件:Babosa@EasyDarwin.org

## 捐赠您自己的项目 ##


**EasyDarwin**从发展至今，从最初单纯的流媒体服务器EasyDarwin，已经发展成为一个非常丰富的流媒体开源社区了，目前EasyDarwin Github所有的开源项目，有EasyDarwin开源团队开发，也有外部开发者贡献给EasyDarwin社区的，并由原始作者和EasyDarwin开源团队一起进行后续的开发和维护；

#### 捐赠的好处 ####

捐赠进入EasyDarwin社区的开源项目，必须命名以Easy前缀打头，项目捐赠进入EasyDarwin开源社区，会有非常庞大的流媒体社区用户使用，帮助您修复项目bug，快速进行功能迭代，并且有开源团队和开源社区的大牛来帮助您解决诸多棘手的问题，对提升项目和个人技术水平，都是非常有益的；

#### 捐赠的方法 ####

捐赠渠道：

1. 发送邮件到：support@easydarwin.org or  babosa@easydarwin.org；
2. 加EasyDarwin项目创始人QQ：289042893 沟通；

记得附上项目具体说明；

## 获取更多信息 ##

邮件：[support@easydarwin.org](mailto:support@easydarwin.org) 

WEB：[www.EasyDarwin.org](http://www.easydarwin.org)

QQ交流群：[288214068](http://jq.qq.com/?_wv=1027&k=2Dlyhr7 "EasyDarwin交流群1") / [496258327](http://jq.qq.com/?_wv=1027&k=2Hyz2ea "EasyDarwin交流群2")

Copyright &copy; www.EasyDarwin.org 2012-2017

![EasyDarwin](http://www.easydarwin.org/skin/easydarwin/images/wx_qrcode.jpg)

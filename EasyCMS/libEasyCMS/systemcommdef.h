/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
#ifndef _SYSTEM_COMMON_DEF_H
#define _SYSTEM_COMMON_DEF_H
#include <string>
using namespace std;

//ϵͳ��Ϣ
#define SERVICE_MSG_NETPACKET 0x01000001
#define SERVICE_MSG_HEARTBEAT 0x01000002
#define SERVICE_MSG_SET_USER_ACCOUNT_CACHE 0x01000003
#define SERVICE_MSG_HTTP_PACKET 0X01000004

#define MSG_SYS_STREAM_AGENT_CONNECT    0x02000001
#define MSG_SYS_STREAM_AGENT_DISCONNECT 0x02000002

#define MSG_SYS_HTTP_AGENT_CONNECT      0x02000003
#define MSG_SYS_HTTP_AGENT_DISCONNECT   0x02000004

#define MSG_SYS_REALPLAY_CONNECT        0X02000005
#define MSG_SYS_REALPLAY_DISCONNECT     0X02000006

//������Ϣ�ṹ��
typedef void* COMMON_SOCKET_HANDLE;
typedef void* COMMON_HANDLE;
typedef unsigned int u32;
typedef unsigned long long u64;

#define EASYDSS_STREAM_TYPE_VIDEO 0
#define EASYDSS_STREAM_TYPE_AUDIO 1

#define EASYDSS_FRAME_TYPE_A   0
#define EASYDSS_FRAME_TYPE_I    1
#define EASYDSS_FRAME_TYPE_P    2

#define STREAM_SYNC_WORD "0EASYDSS"

struct EASYDSS_NET_HEADER
{
	char                     chSynWord[8];      /* ͬ��ͷ,ͬ���ַ���Ϊ"0EASYDSS" */
	char                     chDevTag[12];      /* �豸��ǩ,Ψһ��ʶ�豸�Ĵ���,һ��Ϊ�豸���к� */
	unsigned char            uiStreamType;      /* �������� 0��ʾ��Ƶ 1��ʾ��Ƶ*/
	unsigned char            uiFrameType;       /* ֡���� 0��ʾ��Ƶ 1��ʾI֡ 2��ʾP֡ */
	unsigned char            ucReserved[2];     /* �����ֶΣ���ʼ��Ϊ0*/
	unsigned int             uiPTS;  			/* PTS */
	unsigned int             uiHeadSize;        /* ϵͳͷ���ݴ�С */
	unsigned int             uiBuffSize;        /* �������� */
	unsigned char            ucStreamBuff[];    /* ���������� */
};


typedef struct _NetHeader_T
{
	COMMON_HANDLE		 customHandle_;
	COMMON_SOCKET_HANDLE sockHandle_;
	_NetHeader_T()
	{
		customHandle_ = 0;
		sockHandle_ = 0;
	};
}NetHeader_T,*NetHeaderPtr_T;

typedef struct _NetMessage_T
{
	NetHeader_T header_;
	char szMsgXML[];
}NetMessage_T,*NetMessagePtr_T;


//��������Ϣ

typedef struct _ServicePort_T
{
	int nPort_;
	string strType_;
}ServicePort_T, *ServicePtr_T;

typedef struct _ServiceUnit_T
{
	string strSvcType_;
	string strWanIP_;
	string strLanIP_;
	ServicePort_T ports_[10];
	int    nLoad_;
	int	   nPortNum_;
	_ServiceUnit_T()
	{
		Init();
	};

	void Init()
	{
		strSvcType_.empty();
		strWanIP_.empty();
		strLanIP_.empty();
		nLoad_ = 0;
		nPortNum_ = 0;
	};
}ServiceUnit_T, *ServiceUnitPtr_T;

#define EASY_REDIS_LAN_IP	"LanIP"
#define EASY_REDIS_WAN_IP	"WanIP"
#define EASY_REDIS_LAN_PORT	"LANPort"
#define EASY_REDIS_WAN_PORT	"WANPort"
#define EASY_REDIS_SERVICE_ID	"ServiceID"
#define EASY_REDIS_SERVICE_LOAD	"Load"
#define EASY_REDIS_DEVICE_SERIAL	"DeviceSerial"
#define EASY_REDIS_DEVICE_NAME	"DeviceName"
#define EASY_REDIS_SMS_LAN_REAL_PLAY_PORT	"LANRealPlayPort"
#define	EASY_REDIS_SMS_WAN_REAL_PLAY_PORT	"WANRealPlayPort"
#define EASY_REDIS_DEVICE_PLAYING_AMOUNT	"PlayingAmount"
#define EASY_REDIS_HLS_URL	"URL"

#ifndef printlog 
#define printlog qtss_printf
#endif
#endif
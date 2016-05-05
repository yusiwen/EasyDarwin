/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#include "EasyMediaSource.h"
#include "QTSServerInterface.h"
#include "QTSServer.h"

static unsigned int sLastVPTS = 0;
static unsigned int sLastAPTS = 0;

int __EasyPusher_Callback(int _id, EASY_PUSH_STATE_T _state, EASY_AV_Frame *_frame, void *_userptr)
{
    if (_state == EASY_PUSH_STATE_CONNECTING)               printf("Connecting...\n");
    else if (_state == EASY_PUSH_STATE_CONNECTED)           printf("Connected\n");
    else if (_state == EASY_PUSH_STATE_CONNECT_FAILED)      printf("Connect failed\n");
    else if (_state == EASY_PUSH_STATE_CONNECT_ABORT)       printf("Connect abort\n");
    //else if (_state == EASY_PUSH_STATE_PUSHING)             printf("P->");
    else if (_state == EASY_PUSH_STATE_DISCONNECTED)        printf("Disconnect.\n");

    return 0;
}

//���������Ƶ���ݻص�
HI_S32 NETSDK_APICALL OnStreamCallback(	HI_U32 u32Handle,		/* ��� */
										HI_U32 u32DataType,     /* �������ͣ���Ƶ����Ƶ���ݻ�����Ƶ�������� */
										HI_U8*  pu8Buffer,      /* ���ݰ���֡ͷ */
										HI_U32 u32Length,		/* ���ݳ��� */
										HI_VOID* pUserData		/* �û�����*/
									)						
{
	HI_S_AVFrame* pstruAV = HI_NULL;
	HI_S_SysHeader* pstruSys = HI_NULL;
	EasyMediaSource* pThis = (EasyMediaSource*)pUserData;

	if (u32DataType == HI_NET_DEV_AV_DATA)
	{
		pstruAV = (HI_S_AVFrame*)pu8Buffer;

		if (pstruAV->u32AVFrameFlag == HI_NET_DEV_VIDEO_FRAME_FLAG)
		{
			//ǿ��Ҫ���һ֡ΪI�ؼ�֡
			if(pThis->m_bForceIFrame)
			{
				if(pstruAV->u32VFrameType == HI_NET_DEV_VIDEO_FRAME_I)
					pThis->m_bForceIFrame = false;
				else
					return HI_SUCCESS;
			}

			unsigned int vInter = pstruAV->u32AVFramePTS - sLastVPTS;

			sLastVPTS = pstruAV->u32AVFramePTS;
			pThis->PushFrame((unsigned char*)pu8Buffer, u32Length);
		}
		else if (pstruAV->u32AVFrameFlag == HI_NET_DEV_AUDIO_FRAME_FLAG)
		{
			pThis->PushFrame((unsigned char*)pu8Buffer, u32Length);
		}
	}	

	return HI_SUCCESS;
}

	
HI_S32 NETSDK_APICALL OnEventCallback(	HI_U32 u32Handle,	/* ��� */
										HI_U32 u32Event,	/* �¼� */
										HI_VOID* pUserData  /* �û�����*/
                                )
{
	//if(HI_NET_DEV_NORMAL_DISCONNECTED == u32Event)
	//	pSamnetlibDlg->AlartData();
	//qtss_printf("Event Callback\n");
	return HI_SUCCESS;
}

HI_S32 NETSDK_APICALL OnDataCallback(	HI_U32 u32Handle,		/* ��� */
										HI_U32 u32DataType,		/* ��������*/
										HI_U8*  pu8Buffer,      /* ���� */
										HI_U32 u32Length,		/* ���ݳ��� */
										HI_VOID* pUserData		/* �û�����*/
                                )
{
	//CSamnetlibDlg *pSamnetlibDlg = (CSamnetlibDlg*)pUserData;
	//qtss_printf("Data Callback\n");
	//pSamnetlibDlg->AlartData();
	return HI_SUCCESS;
}


EasyMediaSource::EasyMediaSource()
:	Task(),
	m_u32Handle(0),
	fCameraLogin(false),//�Ƿ��ѵ�¼��ʶ
	m_bStreamFlag(false),//�Ƿ�������ý���ʶ
	m_bForceIFrame(true),
	fPusherHandle(NULL)
{
	//SDK��ʼ����ȫ�ֵ���һ��
	HI_NET_DEV_Init();
	this->Signal(Task::kStartEvent);
}

EasyMediaSource::~EasyMediaSource(void)
{
	qtss_printf("~EasyMediaSource\n");
	
	//��ֹͣStream���ڲ����Ƿ���Stream���ж�
	NetDevStopStream();

	if(fCameraLogin)
		HI_NET_DEV_Logout(m_u32Handle);

	//SDK�ͷţ�ȫ�ֵ���һ��
	HI_NET_DEV_DeInit();
}

bool EasyMediaSource::CameraLogin()
{
	//����ѵ�¼������true
	if(fCameraLogin) return true;
	//��¼�������
	HI_S32 s32Ret = HI_SUCCESS;
	s32Ret = HI_NET_DEV_Login(	&m_u32Handle,
		QTSServerInterface::GetServer()->GetPrefs()->GetRunUserName(),
		QTSServerInterface::GetServer()->GetPrefs()->GetRunPassword(),
		QTSServerInterface::GetServer()->GetPrefs()->GetLocalCameraAddress(),
		QTSServerInterface::GetServer()->GetPrefs()->GetLocalCameraPort());

	if (s32Ret != HI_SUCCESS)
	{
		qtss_printf("HI_NET_DEV_Login Fail\n");
		m_u32Handle = 0;
		return false;
	}
	else
	{
		HI_NET_DEV_SetReconnect(m_u32Handle, 5000);
		fCameraLogin = true;
	}

	return true;
}

QTSS_Error EasyMediaSource::NetDevStartStream()
{
	//���δ��¼,����ʧ��
	if(!CameraLogin()) return QTSS_RequestFailed;
	
	//�Ѿ����������У�����Easy_AttrNameExists
	if(m_bStreamFlag) return QTSS_AttrNameExists;

	OSMutexLocker locker(this->GetMutex());

	QTSS_Error theErr = QTSS_NoErr;
	HI_S32 s32Ret = HI_SUCCESS;
	HI_S_STREAM_INFO struStreamInfo;

	HI_NET_DEV_SetEventCallBack(m_u32Handle, (HI_ON_EVENT_CALLBACK)OnEventCallback, this);
	HI_NET_DEV_SetStreamCallBack(m_u32Handle, (HI_ON_STREAM_CALLBACK)OnStreamCallback, this);
	HI_NET_DEV_SetDataCallBack(m_u32Handle, (HI_ON_DATA_CALLBACK)OnDataCallback, this);

	struStreamInfo.u32Channel = HI_NET_DEV_CHANNEL_1;
	struStreamInfo.blFlag = QTSServerInterface::GetServer()->GetPrefs()->GetCameraStreamType()?HI_TRUE:HI_FALSE;
	struStreamInfo.u32Mode = HI_NET_DEV_STREAM_MODE_TCP;
	struStreamInfo.u8Type = HI_NET_DEV_STREAM_ALL;
	s32Ret = HI_NET_DEV_StartStream(m_u32Handle, &struStreamInfo);
	if (s32Ret != HI_SUCCESS)
	{
		qtss_printf("HI_NET_DEV_StartStream Fail\n");
		return QTSS_RequestFailed;
	}

	m_bStreamFlag = true;
	m_bForceIFrame = true;
	qtss_printf("HI_NET_DEV_StartStream SUCCESS\n");

	return QTSS_NoErr;
}

void EasyMediaSource::NetDevStopStream()
{
	if( m_bStreamFlag )
	{
		qtss_printf("HI_NET_DEV_StopStream\n");
		HI_NET_DEV_StopStream(m_u32Handle);
		m_bStreamFlag = false;
		//m_bForceIFrame = false;
	}
}

void EasyMediaSource::handleClosure(void* clientData) 
{
	if(clientData != NULL)
	{
		EasyMediaSource* source = (EasyMediaSource*)clientData;
		source->handleClosure();
	}
}

void EasyMediaSource::handleClosure() 
{
	if (fOnCloseFunc != NULL) 
	{
		(*fOnCloseFunc)(fOnCloseClientData);
	}
}

void EasyMediaSource::stopGettingFrames() 
{
	OSMutexLocker locker(this->GetMutex());
	fOnCloseFunc = NULL;
	doStopGettingFrames();
}

void EasyMediaSource::doStopGettingFrames() 
{
	qtss_printf("doStopGettingFrames()\n");
	NetDevStopStream();
}

bool EasyMediaSource::GetSnapData(unsigned char* pBuf, UInt32 uBufLen, int* uSnapLen)
{
	//��������δ��¼������false
	if(!CameraLogin()) return false;

	//����SDK��ȡ����
	HI_S32 s32Ret = HI_FAILURE; 
	s32Ret = HI_NET_DEV_SnapJpeg(m_u32Handle, (HI_U8*)pBuf, uBufLen, uSnapLen);
	if(s32Ret == HI_SUCCESS)
	{
		return true;
	}

	return false;
}

SInt64 EasyMediaSource::Run()
{
	QTSS_Error nRet = QTSS_NoErr;

	//���豸��ȡ��������
	unsigned char *sData = (unsigned char*)malloc(EASY_SNAP_BUFFER_SIZE);
	int snapBufLen = 0;

	do{
		//�����ȡ��������������ݣ�Base64����/����
		if(!GetSnapData(sData, EASY_SNAP_BUFFER_SIZE, &snapBufLen))
		{
			//δ��ȡ������
			qtss_printf("EasyDeviceCenter::UpdateDeviceSnap => Get Snap Data Fail \n");
			nRet = QTSS_ValueNotFound;
			break;
		}

		QTSServer* svr = (QTSServer*)QTSServerInterface::GetServer();

		//EasyCMS_UpdateSnap(svr->GetCMSHandle(), (const char*)sData, snapBufLen);

	}while(0);

	free((void*)sData);
	sData = NULL;

	return 2*60*1000;
}


QTSS_Error EasyMediaSource::StartStreaming()
{
	if(NULL == fPusherHandle)
	{
		EASY_MEDIA_INFO_T	mediainfo;
		memset(&mediainfo, 0x00, sizeof(EASY_MEDIA_INFO_T));
		mediainfo.u32VideoCodec =   EASY_SDK_VIDEO_CODEC_H264;
		mediainfo.u32AudioCodec	=	EASY_SDK_AUDIO_CODEC_G711A;
		mediainfo.u32AudioSamplerate = 8000;
		mediainfo.u32AudioChannel = 1;
		mediainfo.u32VideoFps = 25;

		fPusherHandle = EasyPusher_Create();

		EasyPusher_SetEventCallback(fPusherHandle, __EasyPusher_Callback, 0, NULL);

		char sdpName[64] = { 0 };
		sprintf(sdpName,"%s.sdp",QTSServerInterface::GetServer()->GetPrefs()->GetDeviceSerialNumber()); 

		EasyPusher_StartStream(fPusherHandle, QTSServerInterface::GetServer()->GetPrefs()->GetRTSPServerAddr(), 
			QTSServerInterface::GetServer()->GetPrefs()->GetRTSPServerPort(), sdpName, "", "", &mediainfo, 1024, 0);
	}

	NetDevStartStream();

	return QTSS_NoErr;
}

QTSS_Error EasyMediaSource::StopStreaming()
{
	if(fPusherHandle)
	{
		EasyPusher_StopStream(fPusherHandle);
		EasyPusher_Release(fPusherHandle);
		fPusherHandle = 0;
	}

	stopGettingFrames();

	return QTSS_NoErr;
}

QTSS_Error EasyMediaSource::PushFrame(unsigned char* frame, int len)
{	
	if(fPusherHandle == NULL) return QTSS_Unimplemented;

	HI_S_AVFrame* pstruAV = (HI_S_AVFrame*)frame;

	EASY_AV_Frame  avFrame;
	memset(&avFrame, 0x00, sizeof(EASY_AV_Frame));

	if (pstruAV->u32AVFrameFlag == HI_NET_DEV_VIDEO_FRAME_FLAG)
	{
		if(pstruAV->u32AVFrameLen > 0)
		{
			unsigned char* pbuf = (unsigned char*)frame+sizeof(HI_S_AVFrame);

			EASY_AV_Frame  avFrame;
			memset(&avFrame, 0x00, sizeof(EASY_AV_Frame));
			avFrame.u32AVFrameLen = pstruAV->u32AVFrameLen;
			avFrame.pBuffer = (unsigned char*)pbuf;
			avFrame.u32VFrameType = (pstruAV->u32VFrameType==HI_NET_DEV_VIDEO_FRAME_I)?EASY_SDK_VIDEO_FRAME_I:EASY_SDK_VIDEO_FRAME_P;
			avFrame.u32AVFrameFlag = EASY_SDK_VIDEO_FRAME_FLAG;
			avFrame.u32TimestampSec = pstruAV->u32AVFramePTS/1000;
			avFrame.u32TimestampUsec = (pstruAV->u32AVFramePTS%1000)*1000;
			EasyPusher_PushFrame(fPusherHandle, &avFrame);
		}	
	}
	else if (pstruAV->u32AVFrameFlag == HI_NET_DEV_AUDIO_FRAME_FLAG)
	{
		if(pstruAV->u32AVFrameLen > 0)
		{
			unsigned char* pbuf = (unsigned char*)frame+sizeof(HI_S_AVFrame);

			EASY_AV_Frame  avFrame;
			memset(&avFrame, 0x00, sizeof(EASY_AV_Frame));
			avFrame.u32AVFrameLen = pstruAV->u32AVFrameLen-4;//ȥ�������Զ����4�ֽ�ͷ
			avFrame.pBuffer = (unsigned char*)pbuf+4;
			avFrame.u32AVFrameFlag = EASY_SDK_AUDIO_FRAME_FLAG;
			avFrame.u32TimestampSec = pstruAV->u32AVFramePTS/1000;
			avFrame.u32TimestampUsec = (pstruAV->u32AVFramePTS%1000)*1000;
			EasyPusher_PushFrame(fPusherHandle, &avFrame);
		}			
	}
	return Easy_NoErr;
}
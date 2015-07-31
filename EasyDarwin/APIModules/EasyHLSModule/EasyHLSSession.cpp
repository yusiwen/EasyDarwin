/*
	Copyright (c) 2013-2014 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
    File:       EasyHLSSession.cpp
    Contains:   Implementation of object defined in EasyHLSSession.h. 
*/
#include "EasyHLSSession.h"
#include "SocketUtils.h"
#include "EventContext.h"
#include "OSMemory.h"
#include "OS.h"
#include "atomic.h"
#include "QTSSModuleUtils.h"
#include <errno.h>

#ifndef __Win32__
    #include <unistd.h>
#endif

static FILE* fTest;

//NVSource Callback
int CALLBACK __NVSourceCallBack( int _chid, int *_chPtr, int _mediatype, char *pbuf, NVS_FRAME_INFO *frameinfo)
{
	EasyHLSSession* pHLSSession = (EasyHLSSession *)_chPtr;

	if (NULL == pHLSSession)	return -1;

	if (NULL != frameinfo)
	{
		if (frameinfo->height==1088)		frameinfo->height=1080;
		else if (frameinfo->height==544)	frameinfo->height=540;
	}

	//Ͷ�ݵ������EasyHLSSession���д���
	pHLSSession->ProcessData(_chid, _mediatype, pbuf, frameinfo);

	return 0;
}

EasyHLSSession::EasyHLSSession(StrPtrLen* inSourceID)
:   fQueueElem(),
	fNVSHandle(NULL)
{

    fQueueElem.SetEnclosingObject(this);
    if (inSourceID != NULL)
    {
        fSourceID.Ptr = NEW char[inSourceID->Len + 1];
        ::memcpy(fSourceID.Ptr, inSourceID->Ptr, inSourceID->Len);
        fSourceID.Len = inSourceID->Len;
        fRef.Set(fSourceID, this);
    }

	fTest = ::fopen("./aaa.264","wb");
}


EasyHLSSession::~EasyHLSSession()
{
    fSourceID.Delete();
}

QTSS_Error EasyHLSSession::ProcessData(int _chid, int mediatype, char *pbuf, NVS_FRAME_INFO *frameinfo)
{
	if (mediatype == MEDIA_TYPE_VIDEO)
	{
		printf("Get Video Len:%d tm:%d rtp:%d\n", frameinfo->length, frameinfo->timestamp_sec, frameinfo->rtptimestamp);
		::fwrite(pbuf, 1, frameinfo->length, fTest);
	}
	else if (mediatype == MEDIA_TYPE_AUDIO)
	{
		printf("Get Audio Len:%d tm:%d rtp:%d\n", frameinfo->length, frameinfo->timestamp_sec, frameinfo->rtptimestamp);
	}
	else if (mediatype == MEDIA_TYPE_EVENT)
	{
		printf("Get MEDIA_TYPE_EVENT\n");
		if (NULL == pbuf && NULL == frameinfo)
		{
			printf("Connecting:%s ...\n", fSourceID.Ptr);
			//_TRACE("[ch%d]������...\n", _chid);
			//MEDIA_FRAME_INFO	frameinfo;
			//memset(&frameinfo, 0x00, sizeof(MEDIA_FRAME_INFO));
			//frameinfo.length = 1;
			//frameinfo.type   = 0xFF;
			//SSQ_AddData(pRealtimePlayThread[_chid].pAVQueue, _chid, MEDIA_TYPE_EVENT, (MEDIA_FRAME_INFO*)&frameinfo, "1");
		}
		else if (NULL!=frameinfo && frameinfo->type==0xF1)
		{
			printf("Lose Packet:%s ...\n", fSourceID.Ptr);
			//_TRACE("[ch%d]����[%.2f]...\n", _chid, frameinfo->losspacket);
			//frameinfo->length = 1;
			//SSQ_AddData(pRealtimePlayThread[_chid].pAVQueue, _chid, MEDIA_TYPE_EVENT, (MEDIA_FRAME_INFO*)frameinfo, "1");
		}
	}

	return QTSS_NoErr;
}

QTSS_Error	EasyHLSSession::HLSSessionCreate(char* rtspUrl)
{
	NVS_Init(&fNVSHandle);

	if (NULL == fNVSHandle) return QTSS_Unimplemented;

	unsigned int mediaType = MEDIA_TYPE_VIDEO;
	mediaType |= MEDIA_TYPE_AUDIO;	//��ΪNVSource, ��������

	NVS_SetCallback(fNVSHandle, __NVSourceCallBack);
	NVS_OpenStream(fNVSHandle, 0, rtspUrl,RTP_OVER_TCP, mediaType, 0, 0, this, 1000, 0);

	return QTSS_NoErr;
}

QTSS_Error	EasyHLSSession::HLSSessionRelease()
{
	if(NULL == fNVSHandle)	return QTSS_BadArgument;
	NVS_CloseStream(fNVSHandle);
	NVS_Deinit(&fNVSHandle);
	return QTSS_NoErr;
}
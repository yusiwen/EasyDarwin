/*
	Copyright (c) 2013-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
    File:       EasyHLSSession.h
    Contains:   EasyHLSSession
*/
#pragma once
#include "QTSS.h"
#include "OSRef.h"
#include "StrPtrLen.h"

#include "EasyHLSAPI.h"
#include "EasyAACEncoderAPI.h"
#include "TimeoutTask.h"
#include "QTSServerInterface.h"

#ifndef __EASY_HLS_SESSION__
#define __EASY_HLS_SESSION__

class EasyHLSSession : public Task
{
    public:
        EasyHLSSession(StrPtrLen* inName, StrPtrLen* inSourceURL, UInt32 inChannel = 0);
        virtual ~EasyHLSSession();
        static void Initialize(QTSS_ModulePrefsObject inPrefs);

		SInt64	Run() override;

		OSRef*			GetRef() { return &fRef; }
		OSMutex*		GetMutex() { return &fMutex; }

		StrPtrLen*      GetSourceID() { return &fSourceID; }
		StrPtrLen*      GetStreamName() { return &fSessionName; }
		StrPtrLen*		GetSourceURL() { return &fSourceURL; }
		const char*		GetHLSURL() const { return fHLSURL; }
		UInt32			GetChannelNum() const { return fChannelNum; }

		QTSS_Error		ProcessData(int _chid, int mediatype, char *pbuf, RTSP_FRAME_INFO *frameinfo);
		QTSS_Error		SessionStart();
		QTSS_Error		SessionRelease();

		void			RefreshTimeout()	{ fTimeoutTask.RefreshTimeout(); }

		SInt64          GetTotalPlayTime()		const { return fTotalPlayTime; }
		SInt64			GetNumPacketsReceived() const { return fNumPacketsReceived; }
		SInt64			GetNumBytesReceived()	const { return fNumBytesReceived; }
		UInt32			GetLastStatBitrate()	const { return fLastStatBitrate; }
   
private:
		// For storage in the session map       
		OSRef       fRef;
		StrPtrLen   fSourceID;
		StrPtrLen	fSessionName;
		StrPtrLen	fSourceURL;
		char		fHLSURL[QTSS_MAX_URL_LENGTH];
		UInt32		fChannelNum;

		OSMutex		fMutex;
		TimeoutTask	fTimeoutTask;

		Easy_RTSP_Handle		fRTSPClientHandle;
		Easy_HLS_Handle			fHLSHandle;
		EasyAACEncoder_Handle	fAAChandle;

		//Audio
		unsigned long long		fLastAudioPTS;
		QTSS_Error EasyInitAACEncoder(int codec);
		UInt8 pbAACBuffer[EASY_ACCENCODER_BUFFER_SIZE_LEN];

		//hls prefs
		static UInt32	sM3U8Version;
		static bool		sAllowCache;
		static UInt32	sTargetDuration;
		static UInt32	sPlaylistCapacity;

		//stat
		SInt64          fPlayTime;				//��ʼ��ʱ��
		SInt64			fLastStatPlayTime;		//��һ��ͳ�Ƶ�ʱ��
        SInt64          fTotalPlayTime;			//�ܹ�����ʱ��
        SInt64			fNumPacketsReceived;	//�յ������ݰ�������
		SInt64			fLastNumPacketsReceived;//��һ��ͳ���յ������ݰ�����
        SInt64			fNumBytesReceived;		//�յ�����������
        SInt64			fLastNumBytesReceived;	//��һ��ͳ���յ�����������
		UInt32			fLastStatBitrate;		//���һ��ͳ�Ƶõ��ı�����
};

#endif


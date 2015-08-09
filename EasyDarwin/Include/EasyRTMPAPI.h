/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#ifndef _Easy_RTMP_API_H
#define _Easy_RTMP_API_H

#ifdef _WIN32
#define EasyRTMP_API  __declspec(dllexport)
#define Easy_APICALL  __stdcall
#else
#define EasyRTMP_API
#define Easy_APICALL 
#endif

#define Easy_RTMP_Handle void*

#ifdef __cplusplus
extern "C" 
{
#endif
	/* ����RTMP����Session �������;�� */
	EasyRTMP_API Easy_RTMP_Handle Easy_APICALL EasyRTMP_Session_Create();

	/* ����RTMP���͵Ĳ�����Ϣ */
	EasyRTMP_API int Easy_APICALL Easy_APICALL EasyRTMP_InitMetadata(Easy_RTMP_Handle handle,const char *sps, int spsLen, const char *pps, int ppsLen, int videoFrameRate, int audioSampleRate);
	
	/* ����RTMP������ */
	EasyRTMP_API bool Easy_APICALL EasyRTMP_Connect(Easy_RTMP_Handle handle, const char *url);

	/* ����H264 */
	EasyRTMP_API bool Easy_APICALL EasyRTMP_SendH264Packet(Easy_RTMP_Handle handle, unsigned char *data,unsigned int size,bool bIsKeyFrame,unsigned int nTimeStamp);

	/* ����AAC */
	EasyRTMP_API bool Easy_APICALL EasyRTMP_SendAACPacket(Easy_RTMP_Handle handle, unsigned char *data, unsigned int size, unsigned int nTimeStamp);

	/* ֹͣRTMP���ͣ��ͷž�� */
	EasyRTMP_API void Easy_APICALL EasyRTMP_Session_Release(Easy_RTMP_Handle handle);

#ifdef __cplusplus
};
#endif

#endif

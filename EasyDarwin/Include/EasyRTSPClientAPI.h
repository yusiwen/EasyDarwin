/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#ifndef _Easy_RTSPClient_API_H
#define _Easy_RTSPClient_API_H

#include "EasyTypes.h"

/*
	_mediatype:		EASY_SDK_VIDEO_FRAME_FLAG	EASY_SDK_AUDIO_FRAME_FLAG	EASY_SDK_EVENT_FRAME_FLAG	
	�����EasyRTSP_OpenStream�еĲ���outRtpPacket��Ϊ1, ��ص��е�_mediatypeΪEASY_SDK_RTP_FRAME_FLAG, pbufΪ���յ���RTP��(����rtpͷ��Ϣ), frameinfo->lengthΪ����
*/
typedef int (Easy_APICALL *RTSPSourceCallBack)( int _chid, int *_chPtr, int _mediatype, char *pbuf, RTSP_FRAME_INFO *frameinfo);

#ifdef __cplusplus
extern "C"
{
#endif
	/* ��ȡ���һ�δ���Ĵ����� */
	Easy_API int Easy_APICALL EasyRTSP_GetErrCode();

	/* ����RTSPClient���  ����Ϊ���ֵ */
	Easy_API int Easy_APICALL EasyRTSP_Init(Easy_RTSP_Handle *handle);

	/* �ͷ�RTSPClient ����ΪRTSPClient��� */
	Easy_API int Easy_APICALL EasyRTSP_Deinit(Easy_RTSP_Handle *handle);

	/* �������ݻص� */
	Easy_API int Easy_APICALL EasyRTSP_SetCallback(Easy_RTSP_Handle handle, RTSPSourceCallBack _callback);

	/* �������� */
	Easy_API int Easy_APICALL EasyRTSP_OpenStream(Easy_RTSP_Handle handle, int _channelid, char *_url, RTP_CONNECT_TYPE _connType, unsigned int _mediaType, char *_username, char *_password, void *userPtr, int _reconn/*1000��ʾ������,���������Ͽ��Զ�����, ����ֵΪ���Ӵ���*/, int outRtpPacket/*Ĭ��Ϊ0,���ص����������֡, ���Ϊ1,�����RTP��*/);
	
	/* �ر������� */
	Easy_API int Easy_APICALL EasyRTSP_CloseStream(Easy_RTSP_Handle handle);

#ifdef __cplusplus
}
#endif

#endif

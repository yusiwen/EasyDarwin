/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#if !defined _LIB_TINY_DISPATCH_CENTER_H
#define _LIB_TINY_DISPATCH_CENTER_H

#ifdef WIN32
#ifdef DLL_IMPLEMENT
#define DLL_API __declspec(dllexport)
#else
#define DLL_API __declspec(dllimport)
#endif
#else
#define DLL_API
#endif

namespace libTinyDispatchCenter { 
struct DLL_API TinyMsgHead_T
{
	int nMsgType_;
	void *customHandle_;
};

struct DLL_API TinySysMsg_T
{
	TinyMsgHead_T msgHead_;
	bool bUsing_;
	char *msgBody_;
	int  nBuffLen_;
	TinySysMsg_T();
	~TinySysMsg_T();

	void InitMsg(int nBuffLen);
	
};

typedef int (*DispatchMsgThread_T)(unsigned long long ulParam, 
									TinySysMsg_T *pMsg);

typedef void * HDISPATCHTASK;

DLL_API HDISPATCHTASK libDispatch_CreateInstance(
	int nDispatchThreadNum, //��Ϣ�̳߳ص��߳���
	int nMaxWaittingMsg,    //��������������Ϣ����
	int nMsgCacheNum,       //��Ϣ����������Ϣ����
	int nMsgBuffLen,        //��Ϣ���С
	DispatchMsgThread_T func, //��Ϣ�ص�����
	unsigned long long ulParam //��Ϣ�ص��������Զ������
	);

DLL_API int libDispatch_Start(HDISPATCHTASK handle);
DLL_API TinySysMsg_T* libDispatch_GetMsgBuffer(HDISPATCHTASK handle);
DLL_API int libDispatch_SendMsg(HDISPATCHTASK handle, TinySysMsg_T *pMsg);
DLL_API int libDispatch_SendEmptyMsg(HDISPATCHTASK handle, int nMsgType);

}
#endif
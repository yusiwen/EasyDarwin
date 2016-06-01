/*
	Copyright (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
    File:       EasyCMSSession.h
    Contains:   CMS Session
*/

#include "Task.h"
#include "TimeoutTask.h"

#include "QTSSModuleUtils.h"
#include "OSArrayObjectDeleter.h"
#include "OSMemory.h"
#include "QTSSMemoryDeleter.h"
#include "OSRef.h"
#include "StringParser.h"
#include "MyAssert.h"

#include "QTSServerInterface.h"
#include "HTTPProtocol.h"
#include "OSHeaders.h"
#include "QTSS.h"
#include "SocketUtils.h"
#include "EasyProtocol.h"

#include "HTTPRequestStream.h"
#include "HTTPResponseStream.h"
#include "HTTPRequest.h"

using namespace EasyDarwin::Protocol;
using namespace std;

#ifndef __EASY_CMS_SESSION__
#define __EASY_CMS_SESSION__

class EasyCMSSession : public Task
{
public:
    EasyCMSSession();
    virtual ~EasyCMSSession();

	static void Initialize(QTSS_ModulePrefsObject inPrefs);
    
	enum
	{
		kSessionOffline		= 0,	
		kSessionOnline		= 1
	};
	typedef UInt32	SessionStatus;

	SessionStatus GetSessionStatus() { return fSessionStatus; }
	void SetSessionStatus(SessionStatus status) { fSessionStatus = status; }

	// �������¿��ջ���
	QTSS_Error UpdateSnapCache(Easy_CameraSnap_Params* params);

private:
    virtual SInt64 Run();

	void stopPushStream();

	// �����ж�Session Socket�Ƿ�������
	Bool16 isConnected() { return fSocket->GetSocket()->IsConnected(); }

	// transfer error code for http status code
	size_t getStatusNo(QTSS_Error errNo);

	void cleanupRequest();

	// �豸ע�ᵽEasyCMS
	QTSS_Error doDSRegister();

	// �ϴ�����ͼƬ��EasyCMS
	QTSS_Error doDSPostSnap();

	// ����HTTPRequest������
	QTSS_Error processMessage();

	// ���ÿͻ��˲���
	void resetClientSocket();

private:
	enum
	{
		kIdle = 0,
		kReadingMessage = 1,
		kProcessingMessage = 2,
		kSendingMessage = 3,
		kCleaningUp = 4
	};
	UInt32 fState;

	SessionStatus	fSessionStatus;

	TimeoutTask fTimeoutTask;
	ClientSocket* fSocket;

	// ΪCMSSessionר�Ž����������ݰ���ȡ�Ķ���
	HTTPRequestStream   fInputStream;
	// ΪCMSSessionר�Ž����������ݰ����͵Ķ���
	HTTPResponseStream  fOutputStream;

	// ��ʼ��ʱΪNULL
	// ��ÿһ�����󷢳����߽�������ʱ,���п�������HTTPRequest���󲢽��д���
	// ÿһ��״̬�������ڴ������kIdle~kCleanUp�����̶���Ҫ����HTTPRequest����
	HTTPRequest*        fRequest;

	// ��ȡ���籨��ǰ����סSession��ֹ�����ȡ
	OSMutex             fReadMutex;

	// Session��
	OSMutex             fMutex;

	// �����ĵ�Content����
	char*				fContentBuffer;

	// �����ĵ�Content��ȡƫ����,�ڶ�ζ�ȡ������Content����ʱ�õ�
	UInt32				fContentBufferOffset;

	EasyMsgDSPostSnapREQ* fSnapReq;

	// send message count
	unsigned int fSendMessageCount;

	size_t fCSeqCount;

};

#endif


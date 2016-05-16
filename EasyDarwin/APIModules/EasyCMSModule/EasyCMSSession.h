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

#undef COMMON_UTILITIES_LIB

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

	ClientSocket* fSocket;

	TimeoutTask fTimeoutTask;
    
	enum
    {
		kIdle						= 0,
		kReadingMessage				= 1,
		kProcessingMessage          = 2,
		kSendingMessage             = 3,
		kCleaningUp                 = 4
    };
		
	//��Ҫ��CMS��������ͨѶ��������������ͣ���������Ӧ�Ľӿں���
	typedef enum 
	{
		kStreamStopMsg = 0

	}fEnumMsg;

	UInt32 fState;

	void CleanupRequest();

	//��EasyCMS����ֹͣ��������
	QTSS_Error CSFreeStream();

	// ����HTTPRequest������
	QTSS_Error ProcessMessage();
	

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

	//�ӿں���
	void SetStreamStopInfo(const char * chSerial,const char * chChannel);

	void SetMsgType(fEnumMsg msg);
private:
    virtual SInt64 Run();
	char*	fSerial;//��Ҫֹͣ�������豸���к�
	char*   fChannel;//��Ҫֹͣ������ͨ����
	fEnumMsg fMsg;
	Bool16              fLiveSession;

	// �����ж�Session Socket�Ƿ�������
	Bool16 IsConnected() { return fSocket->GetSocket()->IsConnected(); }


};

#endif


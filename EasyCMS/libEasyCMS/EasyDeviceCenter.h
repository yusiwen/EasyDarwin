/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#ifndef _EASY_DEVICE_CENTER_H_
#define _EASY_DEVICE_CENTER_H_

#include "HttpClient.h"
#include "libTinyDispatchCenter.h"
#include <map>
#include "MutexLock.h"
#include "libClientCommondef.h"
#include "ClientSystemCommonDef.h"
#include "QTSS.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#ifndef __Win32__
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#endif

#include "OSMemory.h"
#include "OSArrayObjectDeleter.h"
#include "SocketUtils.h"
#include "StringFormatter.h"
#include "Socket.h"
#include "OS.h"
#include "Task.h"
#include "TimeoutTask.h"
#include "SVector.h"
#include "EasyProtocol.h"
#include "systemcommdef.h"

#include "base64.h"
#include "EasyCMSAPI.h"

using namespace std;
using namespace libTinyDispatchCenter;

namespace EasyDarwin { namespace libEasyCMS
{
typedef void (*HttpClientRecv_Callback)(int nErrorNum, char const* szXML, void *vptr);

/////////////////////////////////////////////////////////////////////////
//class EasyDeviceSession begin

//�ͻ���״̬��
enum
{
	EasyDeviceOffline = 0,
	//easyClientLogging,
	EasyDeviceOnline = 1,
};
typedef UInt32 SessionStatus;

//�洢һ���ͻ��˵Ļ�����Ϣ������ͻ�����Ϣ���շ�������״̬ά��
class EasyDeviceSession : public Task
{
public:
	EasyDeviceSession();
	virtual ~EasyDeviceSession();

	//���ݸ��ϲ㴦��
	void SetRecvDataCallback(HttpClientRecv_Callback func, void *vptr);

	void ResetClient();

	Bool16 IsLiveSession()      { return fSocket->GetSocket()->IsConnected() && fLiveSession; }

	virtual SInt64 Run();

	CHttpClient* GetClient()	{ return m_httpClient; }
	ClientSocket* GetSocket()	{ return fSocket; }

	Easy_Error SetServer(const char *szHost, UInt16 nPort);
	void Disconnect();
	OS_Error SendXML(char* data);

	void SetVersion(const char *szVersion);
	void SetSessionID(const char *szSessionID);
	void SetTerminalType(int eTermissionType);
	void SetAppType(int eAppType);
	void SetDeviceType(int eDeviceType);
	void SetCMSHost(const char *szHost, UInt16 nPort);
	void SetProtocol(EasyDarwinProtocolType protocol) { m_protocol = protocol; }
	void SetSessionStatus(SessionStatus status);
	void SetPublishStreamID(string sStreamID){ m_strPublishStreamID = sStreamID; }

	void SetAccessType(int eLoginType);
	void SetAccess(const char *szAccess);
	void SetPassword(const char *szPassword);

	int GetAccessType();
	const char* GetAccess();
	const char* GetPassword();

	const char* GetVersion();
	const char* GetSessionID();
	int GetTerminalType();
	int GetAppType();
	int GetDeviceType();
	const char* GetCMSHost();
	UInt16 GetCMSHostPort();

	OSMutex*    GetMutex()      { return &fMutex; }
	SessionStatus GetSessionStatus();
	EasyDarwinProtocolType GetProtocol(){ return m_protocol; }
	string GetPublishStreamID(){ return m_strPublishStreamID; }
private:
	//Socket����
	ClientSocket* fSocket;    // Connection object
	//HTTPClient����
	CHttpClient* m_httpClient;
	//��ʱ���
	TimeoutTask fClientTimeoutTask; 

	//�ỰID
	string m_strSessionID;

	//�ͻ������豸�����û�
	int m_iTerminalType;
	
	//��Ϊ�û���¼ʱ���ͻ�������
	int m_iAppType;
	
	//��Ϊ�豸�ͻ���ʱ��m_strTermissionType ��ֵΪ Camera����Device ʱ����Ч
	int m_iDeviceType;

	int m_iAccessType;
	string m_strAccess;
	string m_strPassword;
	
	//XMLЭ��汾
	string m_strXMLVersion;

	//�ͻ�����Ϣ����������
	OSMutex fMutex;

	//CMS��������Ϣ
	string m_strCMSHost;
	UInt16 m_nCMSPort;

	//�ͻ���״̬
	SessionStatus  m_state;

	HttpClientRecv_Callback m_recvCallback;
	void *m_recvCallbackParam;

	EasyDarwinProtocolType m_protocol;
	string m_strPublishStreamID;

protected:
	Bool16              fLiveSession;

};
//class EasyDeviceSession end
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
//class EasyDeviceCenter begin
typedef struct _callback_param_t 
{
	void *pSessionData_;
	void *pMsgCenterData_;
}callback_param_t, *callback_param_ptr_t;


//�ͻ��˵��߼�����
class EasyDeviceCenter : public Task
{
public:
	EasyDeviceCenter();
	virtual ~EasyDeviceCenter(void);

private:
	//�豸����ƽ̨(CMS)�Խ�Session
	EasyDeviceSession* m_deviceSession;
	//�豸����ý�����(SMS)�Խ�Session
	//EasyMediaSession *m_mediaSession;
	//�ڲ�Task��������
	HDISPATCHTASK    m_dispatchTackHandler;

public:
	//��ʼ��
	void InitClient();
	//�豸��¼
	virtual Easy_Error Login(const char *szHost, int nPort, const char *szAccess, const char *szPassword);
	//�豸����
	virtual Easy_Error DeviceHeartBeat();
	//�ϴ�����
	virtual Easy_Error UpdateDeviceSnap(const char* sData, unsigned int snapBufLen);
	//�����Ʒ�������ڣ�Ҳ����CGE��ַ�Ͷ˿�
	virtual void LoginAck(ClientMsgType msgType, NetMessagePtr_T pNetMsg);
	virtual void HeartbeatAck(ClientMsgType msgType, NetMessagePtr_T pNetMsg);
	virtual void PublishStreamReq(ClientMsgType msgType, NetMessagePtr_T pNetMsg);
	virtual void PublishStreamStartAck(ClientMsgType msgType, NetMessagePtr_T pNetMsg);

	//��������SysMsg�������
	static int DispatchMsgCenter(unsigned long long ulParam, TinySysMsg_T *pMsg);
	//���е����������������ɴ˻ص���ɣ���������Ͽ���
	static void HttpRecvData(Easy_Error nErrorNum, char const* szMsg, void *pData);
	//��DispatchMsgCenter��ȡ����Msg Buffer�Ե���
	TinySysMsg_T* GetMsgBuffer();
	//��DispatchMsgCenter�ɷ���Ϣ
	int SendMsg(TinySysMsg_T* pMsg);
	//Easy_InvalidSocket��Ϣ����
	void InvalidSocketHandler();

	EasyDeviceSession* GetDeviceSession() { return m_deviceSession; }
	//EasyMediaSession* GetMediaSession() { return m_mediaSession; }

	void SetEventCallBack(EasyCMS_Callback fCallBack, void *pUserData);

private:
	////��������SysMsg�������
	//void DispatchMsgCenter_(TinySysMsg_T *pMsg);
	//������յ������籨������Ϣ
	void DispatchNetMsgCenter_(NetMessagePtr_T pNetMsg);
	//ִ��SYSTEM_MSG_LOGINϵͳ��Ϣ
	void ExecMsgLogin_(TinySysMsg_T *pMsg);
	//ִ��SYSTEM_MSG_PUBLISH_STREAMϵͳ��Ϣ
	void ExecMsgPublishStream_(TinySysMsg_T *pMsg);
	//ִ��SYSTEM_MSG_HEARTBEATϵͳ��Ϣ
	void ExecMsgHeartBeat_(TinySysMsg_T *pMsg);

	SInt64 Run();
	TimeoutTask *fHeartbeatTask;//�����¼�

	UInt32 fHeartbeatFailTimes;
	int fMsgSendStats;
	UInt32 fState;
	EasyCMS_Callback fEvent;
	void *fEventUser;
};
//class EasyDeviceCenter end
/////////////////////////////////////////////////////////////////////////
}
}
#endif
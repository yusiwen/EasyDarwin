/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/

/*
    File:       HttpClient.h

    Contains:   HttpClient�����շ�http�������ݣ������շ���ý������
*/

#ifndef _HTTP_CLIENT_H
#define _HTTP_CLIENT_H
#include "ClientSocket.h"
#include <string>
#include "HTTPProtocol.h"
#include "OSHeaders.h"
#include "QTSS.h"

using namespace std;

namespace EasyDarwin { namespace libEasyCMS
{

typedef void (*HttpClientError_Callback)(int nErrorNum, void *vptr);
typedef void (*HttpClientRecv_Callback)(int nErrorNum, char const* sErrorString, void *vptr);

class CHttpClient
{
public:
	CHttpClient(ClientSocket* inSocket, Bool16 verbosePrinting = false);
	~CHttpClient();

	void	Set(const StrPtrLen& inURL);
public:	
	OS_Error	SendPacket(char *resXML);
	OS_Error    ReceivePacket();

	OS_Error	SendHttpPacket(char *respXML = NULL);
	OS_Error    DoTransaction();

	OS_Error	SendBytes(char* bytes, unsigned int len);

	StrPtrLen*  GetURL()                { return &fURL; }
    UInt32      GetStatus()             { return fStatus; }
    StrPtrLen*  GetSessionID()          { return &fSessionID; }
    UInt16      GetServerPort()         { return fServerPort; }
    UInt32      GetContentLength()      { return fContentLength; }
    char*       GetContentBody()        { return fRecvContentBuffer; }
	ClientSocket*		GetSocket()     { return fSocket; }
	void		SetSocket(ClientSocket* inSocket) { fSocket = inSocket; }
	OSMutex*            GetMutex()      { return &fMutex; }

	enum
    {
		kMethodBuffLen = 24 //buffer for "POST" or "GET" etc.
    };

	char*       GetResponse()           { return fRecvHeaderBuffer; }
    UInt32      GetResponseLen()        { return fHeaderLen; }
    Bool16      IsTransactionInProgress() { return fState != kInitial; }
	void		ResetRecvBuf();

private:
	ClientSocket*   fSocket;
	UInt32          fVerboseLevel;

	OS_Error    ReceiveResponse();

	OSMutex			fMutex;//this data structure is shared!

	// Information we need to send the request
	StrPtrLen   fURL;
	UInt32      fCSeq;

    // Data buffers
    char        fMethod[kMethodBuffLen]; // holds the current method
    char        fSendBuffer[EASY_REQUEST_BUFFER_SIZE_LEN + 1];   // for sending requests
    char        fRecvHeaderBuffer[EASY_REQUEST_BUFFER_SIZE_LEN + 1];// for receiving response headers
    char*       fRecvContentBuffer;             // for receiving response body

    // Tracking the state of our receives
    UInt32      fContentRecvLen;
    UInt32      fHeaderRecvLen;
    UInt32      fHeaderLen;
    UInt32      fSetupTrackID;					//is valid during a Setup Transaction

	// Response data we get back
    UInt32      fStatus;
    StrPtrLen   fSessionID;
    UInt16      fServerPort;
    UInt32      fContentLength;
    
	//HTTPClient����ģʽִ��һ������/������Ӧ������
    enum 
	{ 
		kInitial,			//Initial
		kRequestSending,	//Request Sending
		kResponseReceiving, //Response Receiving
		kHeaderReceived		//HeaderReceived
	};
    UInt32      fState;

};

}}

#endif

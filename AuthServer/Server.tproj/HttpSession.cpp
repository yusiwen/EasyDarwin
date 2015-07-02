/*
	Copyleft (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
    File:       ServiceSession.cpp
    Contains:   ʵ�ֶԷ���Ԫÿһ��Session�Ự�����籨�Ĵ���
*/

#include "HttpSession.h"
#include "QTSServerInterface.h"
#include "OSMemory.h"
#include "OSArrayObjectDeleter.h"

#if __FreeBSD__ || __hpux__	
    #include <unistd.h>
#endif

#include <errno.h>

#if __solaris__ || __linux__ || __sgi__	|| __hpux__
    #include <crypt.h>
#endif

static StrPtrLen	sServiceStr("HttpServer");

CHttpSession::CHttpSession()
: HttpSessionInterface(),
  fRequest(NULL),
  fReadMutex(),
  fCurrentModule(0),
  fState(kReadingRequestHead)
{
	this->SetTaskName("HTTPSession");

	//��ȫ�ַ��������Session������һ��
	QTSServerInterface::GetServer()->AlterCurrentServiceSessionCount(1);

	fModuleState.curModule = NULL;
	fModuleState.curTask = this;
	fModuleState.curRole = 0;
	fModuleState.globalLockRequested = false;

	qtss_printf("<------New Http Session:%s ------>\n", fSessionID);
}

CHttpSession::~CHttpSession()
{
	char remoteAddress[20] = {0};
	StrPtrLen theIPAddressStr(remoteAddress,sizeof(remoteAddress));
	QTSS_GetValue(this, qtssHttpSesRemoteAddrStr, 0, (void*)theIPAddressStr.Ptr, &theIPAddressStr.Len);

	char msgStr[2048] = { 0 };
	qtss_snprintf(msgStr, sizeof(msgStr), "Http Session Offline from ip[%s]",remoteAddress);
	QTSServerInterface::LogError(qtssMessageVerbosity, msgStr);

    	// Invoke the session closing modules
       	//�Ự�Ͽ�ʱ������ģ�����һЩֹͣ�Ĺ���
    	QTSS_RoleParams theParams;
	theParams.httpSessionClosingParams.inHttpSession = this;
	for (UInt32 x = 0; x < QTSServerInterface::GetNumModulesInRole(QTSSModule::kHttpSessionClosingRole); x++)
       		(void)QTSServerInterface::GetModule(QTSSModule::kHttpSessionClosingRole, x)->CallDispatch(QTSS_HttpSessionClosing_Role, &theParams);

	fLiveSession = false; 
	this->CleanupRequest();		
}

//->geyijyn@20150702
//---������״̬�������л���(�ǳ���Ҫ)
//<-
SInt64 CHttpSession::Run()
{
	//��ȡ�¼�����
	EventFlags events = this->GetEvents();
	QTSS_Error err = QTSS_NoErr;
	QTSSModule* theModule = NULL;
	UInt32 numModules = 0;

	// Some callbacks look for this struct in the thread object
	//��ģ��״̬���̰߳󶨣������Ļ���ģ����
	//�Ϳ���֪���̵߳���Ϣ��
	OSThreadDataSetter theSetter(&fModuleState, NULL);

	//check for a timeout or a kill. If so, just consider the session dead
	if ((events & Task::kTimeoutEvent) || (events & Task::kKillEvent))
       	{
       		fLiveSession = false;

		//�ǵ���־��Ϣ
		char msgStr[128];
		qtss_snprintf(msgStr, sizeof(msgStr), "Service Session Timeout, No Handler\n");
		QTSServerInterface::LogError(qtssMessageVerbosity, msgStr);
	}

	//�����¼���������
	//�����ѭ����ֻ������Ҫ�ȴ��¼���ʱ��Ż��˳�
	//������˳���ʱ��û��RequestEvent()  ��ô�Ͳ��������
	while (this->IsLiveSession())
	{
		//���Ĵ�����״̬������ʽ�����Է����δ���ͬһ����Ϣ
		switch (fState)
		{
		case kReadingRequestHead:
			{
				if ((err = fInputStream.ReadRequest()) == QTSS_NoErr)
				{
					//http ��Ϣͷû�ж��������������
					fInputSocketP->RequestEvent(EV_RE);
					return 0;
				}
				if (err != QTSS_RequestArrived)
				{
					//�����http ��Ϣͷ�Ĺ����г�����
					//��ôֱ�Ӱ����session  ��������
					//ע:::  ԭ���Ĵ�������ϸ�ִ�������
					//��������������Ǳ��ʲô����
					//������������(����Ͽ���)����ô
					//ͨ��Assert(!this->IsLiveSession());break;
					//���˳�while() ֮�󷵻�-1 ,���ﵽɾ��session Ŀ��
					//�������һ���߼���
					//ֻҪ��������ʲô��һ�ɷ�����session.
					return 0;		//===> �ȴ���ʱɾ��
					
				}
				//�����Ϣͷ�����ˣ����л�״̬����Ϣ��
				Assert(err == QTSS_RequestArrived);				
				fState = kReadingRequestBody;
			}
			break;
		case kReadingRequestBody:
			{
				Assert(fInputStream.GetRequestBuffer() );
				if(fRequest == NULL)
				{
					//������Ϣͷ�������Ϣ��ĳ��ȣ���������Ϣ��
					fRequest = NEW HTTPRequest(&sServiceStr, fInputStream.GetRequestBuffer());					
				}

				//������Ϣͷ��ȡ��Ϣ�������
				QTSS_Error theErr = SetupRequest();
				if(theErr == QTSS_WouldBlock)
				{
					//��Ϣ�廹û����ɣ������ȴ�
					fInputSocketP->RequestEvent(EV_RE);
					return 0;
				}
				if (err != QTSS_RequestArrived)
				{
					//�ڶ���Ϣ��Ĺ����г�����
					return 0;		//===> �ȴ���ʱɾ��
				}
				Assert(err == QTSS_RequestArrived);
				fState = kProcessingRequest;
			}
			break;
		case kProcessingRequest:
			{
				//���������Ϣ�Ѿ�������ɣ����Դ�����.
				
				//ˢ��Session����ʱ��
				fTimeoutTask.RefreshTimeout();
				
				//����������Ѿ���ȡ��һ��������Request����׼����������Ĵ���ֱ����Ӧ���ķ���
				//�ڴ˹����У���Session��Socket  �������κ��������ݵĶ�/д��
				fReadMutex.Lock();
				fOutputStream.ResetBytesWritten();

				fRoleParams.httpProcessParams.inHttpSession = this;
				fRoleParams.httpProcessParams.inHttpRequest = fRequest;

				// If no preprocessor sends a response, move onto the request processing module. It
				// is ALWAYS supposed to send a response, but if it doesn't, we have a canned error
				// to send back.
				fModuleState.eventRequested = false;
				fModuleState.idleTime = 0;
				if (QTSServerInterface::GetNumModulesInRole(QTSSModule::kHttpRequestRole) > 0)
				{
					if (fModuleState.globalLockRequested )
					{   
						fModuleState.globalLockRequested = false;
						fModuleState.isGlobalLocked = true;
					}
		
					theModule = QTSServerInterface::GetModule(QTSSModule::kHttpRequestRole, 0);
					(void)theModule->CallDispatch(QTSS_HttpRequest_Role, &fRoleParams);

					fModuleState.isGlobalLocked = false;
					if (fModuleState.globalLockRequested) // call this request back locked
						return this->CallLocked();

					// If this module has requested an event, return and wait for the event to transpire
					if (fModuleState.eventRequested)
					{
						this->ForceSameThread();    // We are holding mutexes, so we need to force
						return fModuleState.idleTime; // If the module has requested idle time...
					}
				}
				fState = kSendingResponse;
			}
			break;
		case kSendingResponse:
			{
				//��Ӧ���ķ��ͣ�ȷ����ȫ����
				Assert(fRequest != NULL);

				//������Ӧ����
				err = fOutputStream.Flush();
				if (err == EAGAIN)
				{
					//����յ�Socket EAGAIN������ô������Ҫ��Socket�ٴο�д��ʱ���ٵ��÷���
					fSocket.RequestEvent(EV_WR);

					//��Ϊǰ��״̬���Ѿ�fReadMutex.lock()��Ҳ����˵״̬��������
					//��ô�ͱ���Ҫ��֤��ͬ���߳�������ͬһ��״̬��
					this->ForceSameThread();	
					return 0;
				}
				else if (err != QTSS_NoErr)
				{
					// Any other error means that the client has disconnected, right?
					Assert(!this->IsLiveSession());
					break;	//
				}
				fState = kCleaningUp;
			}
			break;	
		case kCleaningUp:
			{
				// Cleaning up consists of making sure we've read all the incoming Request Body
				// data off of the socket
				err = this->DumpRequestData();
				if (err == EAGAIN)
				{
					fInputSocketP->RequestEvent(EV_RE);
					this->ForceSameThread();    	// We are holding mutexes, so we need to force
											// the same thread to be used for next Run()
					return 0;
				}
				this->CleanupRequest();
				fState = kReadingRequestHead;
			}
			break;
		}
	} 
	
	// Make absolutely sure there are no resources being occupied by the session
	// at this point.
	this->CleanupRequest();

	// Only delete if it is ok to delete!
	if (fObjectHolders == 0)
       		return -1;

	// If we are here because of a timeout, but we can't delete because someone
	// is holding onto a reference to this session, just reschedule the timeout.
	//
	// At this point, however, the session is DEAD.
	return 0;
}

QTSS_Error CHttpSession::SendHTTPPacket(StrPtrLen* content, Bool16 connectionClose)
{
	//������Ӧ����(HTTPͷ)
	HTTPRequest httpAck(&sServiceStr);
	httpAck.CreateResponseHeader(content->Len?httpOK:httpNotImplemented);
	if (content->Len)
		httpAck.AppendContentLengthHeader(content->Len);
	if(connectionClose)
		httpAck.AppendConnectionCloseHeader();

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteResponseHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);
	
	HttpResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (content->Len > 0) 
		pOutputStream->Put(content->Ptr, content->Len);

	if (pOutputStream->GetBytesWritten() != 0)
	{
		pOutputStream->Flush();
	}
	if(connectionClose)
		this->Signal(Task::kKillEvent);
	return QTSS_NoErr;
}

/*
	Content���Ķ�ȡ�����
	ͬ�����б��Ĵ�������ظ�����
*/
QTSS_Error CHttpSession::SetupRequest()
{
	Assert(fRequest);

	//����������
	QTSS_Error theErr = fRequest->Parse();
	if (theErr != QTSS_NoErr)
		return QTSS_BadArgument;

	//��ȡ����Content json���ݲ���
	StrPtrLen* lengthPtr = fRequest->GetHeaderValue(httpContentLengthHeader);
	StringParser theContentLenParser(lengthPtr);
	theContentLenParser.ConsumeWhitespace();
	UInt32 content_length = theContentLenParser.ConsumeInteger(NULL);
	qtss_printf("ServiceSession read content-length:%d \n", content_length);
	if (content_length <= 0)
		return QTSS_BadArgument;

	//
	// Check for the existence of 2 attributes in the request: a pointer to our buffer for
	// the request body, and the current offset in that buffer. If these attributes exist,
	// then we've already been here for this request. If they don't exist, add them.
	UInt32 theBufferOffset = 0;
	char* theRequestBody = NULL;
	UInt32 theLen = 0;
	theLen = sizeof(theRequestBody);
	theErr = QTSS_GetValue(this, qtssHttpSesContentBody, 0, &theRequestBody, &theLen);
	if (theErr != QTSS_NoErr)
	{
		// First time we've been here for this request. Create a buffer for the content body and
		// shove it in the request.
		theRequestBody = NEW char[content_length + 1];
		memset(theRequestBody,0,content_length + 1);
		theLen = sizeof(theRequestBody);
		theErr = QTSS_SetValue(this, qtssHttpSesContentBody, 0, &theRequestBody, theLen);// SetValue creates an internal copy.
		Assert(theErr == QTSS_NoErr);

		// Also store the offset in the buffer
		theLen = sizeof(theBufferOffset);
		theErr = QTSS_SetValue(this, qtssHttpSesContentBodyOffset, 0, &theBufferOffset, theLen);
		Assert(theErr == QTSS_NoErr);
	}
	theLen = sizeof(theBufferOffset);
	theErr = QTSS_GetValue(this, qtssHttpSesContentBodyOffset, 0, &theBufferOffset, &theLen);

	// We have our buffer and offset. Read the data.
	theErr = fInputStream.Read(theRequestBody + theBufferOffset, content_length - theBufferOffset, &theLen);
	Assert(theErr != QTSS_BadArgument);
	if (theErr == QTSS_RequestFailed)
	{
		OSCharArrayDeleter charArrayPathDeleter(theRequestBody);
		return QTSS_RequestFailed;
	}
    
	qtss_printf("Add Len:%d \n", theLen);
	if ((theErr == QTSS_WouldBlock) || (theLen < ( content_length - theBufferOffset)))
	{
		//
		// Update our offset in the buffer
		theBufferOffset += theLen;
		(void)QTSS_SetValue(this, qtssHttpSesContentBodyOffset, 0, &theBufferOffset, sizeof(theBufferOffset));
		// The entire content body hasn't arrived yet. Request a read event and wait for it.
		Assert(theErr == QTSS_NoErr);
		return QTSS_WouldBlock;
	}
	Assert(theErr == QTSS_NoErr);
	return QTSS_RequestArrived;
}

void CHttpSession::CleanupRequest()
{
	char* theRequestBody = NULL;
	UInt32 theLen = 0;
	theLen = sizeof(theRequestBody);
	QTSS_Error theErr = QTSS_GetValue(this, qtssHttpSesContentBody, 0, &theRequestBody, &theLen);
	if ((theErr != QTSS_NoErr)&&(theRequestBody))
	{
		delete [] theRequestBody;
	}
	UInt32 offset = 0;
	QTSS_SetValue(this, qtssHttpSesContentBodyOffset, 0, &offset, sizeof(offset));
	char* content = NULL;
	QTSS_SetValue(this, qtssHttpSesContentBody, 0, &content, 0);

	if (fRequest != NULL)
	{
		// NULL out any references to the current request
		delete fRequest;
		fRequest = NULL;
	}
	fReadMutex.Unlock();
}

Bool16 CHttpSession::OverMaxConnections(UInt32 buffer)
{
	QTSServerInterface* theServer = QTSServerInterface::GetServer();
	SInt32 maxConns = theServer->GetPrefs()->GetMaxConnections();
	Bool16 overLimit = false;

	if (maxConns > -1) // limit connections
	{ 
		UInt32 maxConnections = (UInt32) maxConns + buffer;
		if  ( theServer->GetNumServiceSessions() > maxConnections ) 
		{
			overLimit = true;          
		}
	}
	return overLimit;
}

QTSS_Error CHttpSession::DumpRequestData()
{
    char theDumpBuffer[QTSS_MAX_HTTP_REQUEST_LENGTH];
    QTSS_Error theErr = QTSS_NoErr;
    while (theErr == QTSS_NoErr)
        theErr = this->Read(theDumpBuffer, QTSS_MAX_HTTP_REQUEST_LENGTH, NULL);
    return theErr;
}


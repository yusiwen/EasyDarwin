/*
	Copyleft (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
	File:       HTTPSession.cpp
	Contains:   ʵ�ֶԷ���Ԫÿһ��Session�Ự�����籨�Ĵ���
*/

#include "HTTPSession.h"
#include "QTSServerInterface.h"
#include "OSMemory.h"
#include "EasyUtil.h"

#include "OSArrayObjectDeleter.h"
#include <boost/algorithm/string.hpp>
#include "QueryParamList.h"

#if __FreeBSD__ || __hpux__	
#include <unistd.h>
#endif

#include <errno.h>

#if __solaris__ || __linux__ || __sgi__	|| __hpux__
#include <crypt.h>
#endif

using namespace std;

static const int sWaitDeviceRspTimeout = 10;

HTTPSession::HTTPSession( )
	: HTTPSessionInterface(),
	fRequest(NULL),
	fReadMutex(),
	fCurrentModule(0),
	fState(kReadingFirstRequest)
{
	this->SetTaskName("HTTPSession");

	//����HTTPSession����(����EasyCameraSession/EasyNVRSession/EasyHTTPSession����)
	QTSServerInterface::GetServer()->AlterCurrentHTTPSessionCount(1);

	fModuleState.curModule = NULL;
	fModuleState.curTask = this;
	fModuleState.curRole = 0;
	fModuleState.globalLockRequested = false;

	//fDeviceSnap = NEW char[EASY_MAX_URL_LENGTH];
	//fDeviceSnap[0] = '\0';

	qtss_printf("Create HTTPSession:%s\n", fSessionID);
}

HTTPSession::~HTTPSession()
{
	fLiveSession = false; //used in Clean up request to remove the RTP session.
	this->CleanupRequest();// Make sure that all our objects are deleted
	
	QTSServerInterface::GetServer()->AlterCurrentHTTPSessionCount(-1);

	//if (fDeviceSnap != NULL)
		//delete [] fDeviceSnap;

	qtss_printf("Release HTTPSession:%s\n", fSessionID);
}

SInt64 HTTPSession::Run()
{
	EventFlags events = this->GetEvents();
	QTSS_Error err = QTSS_NoErr;
	QTSSModule* theModule = NULL;
	UInt32 numModules = 0;
	// Some callbacks look for this struct in the thread object
	OSThreadDataSetter theSetter(&fModuleState, NULL);

	if (events & Task::kKillEvent)
		fLiveSession = false;

	if(events & Task::kTimeoutEvent)
	{
		char msgStr[512];
		qtss_snprintf(msgStr, sizeof(msgStr), "Timeout HTTPSession��Device_serial[%s]\n", fDevice.serial_.c_str());
		QTSServerInterface::LogError(qtssMessageVerbosity, msgStr);
		fLiveSession = false;
	}

	while (this->IsLiveSession())
	{
		switch (fState)
		{
		case kReadingFirstRequest:
			{
				if ((err = fInputStream.ReadRequest()) == QTSS_NoErr)
				{
					fInputSocketP->RequestEvent(EV_RE);
					return 0;
				}

				if ((err != QTSS_RequestArrived) && (err != E2BIG))
				{
					// Any other error implies that the client has gone away. At this point,
					// we can't have 2 sockets, so we don't need to do the "half closed" check
					// we do below
					Assert(err > 0); 
					Assert(!this->IsLiveSession());
					break;
				}

				if ((err == QTSS_RequestArrived) || (err == E2BIG))
					fState = kHaveCompleteMessage;
			}
			continue;

		case kReadingRequest:
			{
				OSMutexLocker readMutexLocker(&fReadMutex);

				if ((err = fInputStream.ReadRequest()) == QTSS_NoErr)
				{
					//���RequestStream����QTSS_NoErr���ͱ�ʾ�Ѿ���ȡ��Ŀǰ���������������
					//���������ܹ���һ�����屨�ģ���Ҫ�����ȴ���ȡ...
					fInputSocketP->RequestEvent(EV_RE);
					return 0;
				}

				if ((err != QTSS_RequestArrived) && (err != E2BIG) && (err != QTSS_BadArgument))
				{
					//Any other error implies that the input connection has gone away.
					// We should only kill the whole session if we aren't doing HTTP.
					// (If we are doing HTTP, the POST connection can go away)
					Assert(err > 0);
					if (fOutputSocketP->IsConnected())
					{
						// If we've gotten here, this must be an HTTP session with
						// a dead input connection. If that's the case, we should
						// clean up immediately so as to not have an open socket
						// needlessly lingering around, taking up space.
						Assert(fOutputSocketP != fInputSocketP);
						Assert(!fInputSocketP->IsConnected());
						fInputSocketP->Cleanup();
						return 0;
					}
					else
					{
						Assert(!this->IsLiveSession());
						break;
					}
				}
				fState = kHaveCompleteMessage;
			}
		case kHaveCompleteMessage:
			{
				Assert( fInputStream.GetRequestBuffer() );

				Assert(fRequest == NULL);
				//���ݾ��������Ĺ���HTTPRequest������
				fRequest = NEW HTTPRequest(&QTSServerInterface::GetServerHeader(), fInputStream.GetRequestBuffer());

				//����������Ѿ���ȡ��һ��������Request����׼����������Ĵ���ֱ����Ӧ���ķ���
				//�ڴ˹����У���Session��Socket�������κ��������ݵĶ�/д��
				fReadMutex.Lock();
				fSessionMutex.Lock();

				//��շ��ͻ�����
				fOutputStream.ResetBytesWritten();

				//�������󳬹��˻�����������Bad Request
				if ((err == E2BIG) || (err == QTSS_BadArgument))
				{
					ExecNetMsgErrorReqHandler(httpBadRequest);
					fState = kSendingResponse;
					break;
				}

				Assert(err == QTSS_RequestArrived);
				fState = kFilteringRequest;
			}

		case kFilteringRequest:
			{
				//ˢ��Session����ʱ��
				fTimeoutTask.RefreshTimeout();

				//�������Ľ��н���
				QTSS_Error theErr = SetupRequest();
				//��SetupRequest����δ��ȡ�����������籨�ģ���Ҫ���еȴ�
				if(theErr == QTSS_WouldBlock)
				{
					this->ForceSameThread();
					fInputSocketP->RequestEvent(EV_RE);
					// We are holding mutexes, so we need to force
					// the same thread to be used for next Run()
					return 0;//����0��ʾ���¼��Ž���֪ͨ������>0��ʾ�涨�¼������Run

				}
				//TODO:Ӧ���پͼ���theErr���жϣ�QTSS_NoErr��Ӧ��ֱ�ӶϿ����ӻ��߷��ش����룬�������治һ�����õ���Ч������
				//

				//ÿһ���������Ӧ�����Ƿ�����ɣ������ֱ�ӽ��лظ���Ӧ
				if (fOutputStream.GetBytesWritten() > 0)
				{
					fState = kSendingResponse;
					break;
				}

				fState = kPreprocessingRequest;
				break;
			}

		case kPreprocessingRequest:
			{
				QTSS_Error theErr = ProcessRequest();//��������

				if (fOutputStream.GetBytesWritten() > 0)//ÿһ���������Ӧ�����Ƿ�����ɣ������ֱ�ӽ��лظ���Ӧ
				{
					delete[] fRequestBody;//�ͷ����ݲ���
					fRequestBody = NULL;
					fState = kSendingResponse;
					break;
				}
				//�ߵ���˵��û�н���������Ӧ���ǵȴ�����HTTPSession�Ļ�Ӧ
				if(fInfo.uWaitingTime>0)
				{
					this->ForceSameThread();
					// We are holding mutexes, so we need to force
					// the same thread to be used for next Run()
					UInt32 iTemp=fInfo.uWaitingTime;
					fInfo.uWaitingTime = 0;//�´εȴ�ʱ����Ҫ���±���ֵ
					return iTemp;//�ȴ���һ�����ڱ�����
				}
				delete[] fRequestBody;//�ͷ����ݲ���,ע���ڴ˿���ָ��Ϊ��
				fRequestBody = NULL;
				fState = kCleaningUp;
				break;
			}

		case kProcessingRequest:
			{
				if (fOutputStream.GetBytesWritten() == 0)
				{
					//����HTTP����
					ExecNetMsgErrorReqHandler(httpInternalServerError);
					fState = kSendingResponse;
					break;
				}

				fState = kSendingResponse;
			}
		case kSendingResponse:
			{
				Assert(fRequest != NULL);

				//������Ӧ����
				err = fOutputStream.Flush();

				if (err == EAGAIN)
				{
					// If we get this error, we are currently flow-controlled and should
					// wait for the socket to become writeable again
					//����յ�Socket EAGAIN������ô������Ҫ��Socket�ٴο�д��ʱ���ٵ��÷���
					fSocket.RequestEvent(EV_WR);
					this->ForceSameThread();
					// We are holding mutexes, so we need to force
					// the same thread to be used for next Run()
					return 0;
				}
				else if (err != QTSS_NoErr)
				{
					// Any other error means that the client has disconnected, right?
					Assert(!this->IsLiveSession());
					break;
				}

				fState = kCleaningUp;
			}

		case kCleaningUp:
			{
				// Cleaning up consists of making sure we've read all the incoming Request Body
				// data off of the socket
				if (this->GetRemainingReqBodyLen() > 0)
				{
					err = this->DumpRequestData();

					if (err == EAGAIN)
					{
						fInputSocketP->RequestEvent(EV_RE);
						this->ForceSameThread();    // We are holding mutexes, so we need to force
						// the same thread to be used for next Run()
						return 0;
					}
				}

				//һ������Ķ�ȡ��������Ӧ�����������ȴ���һ�����籨�ģ�
				this->CleanupRequest();
				fState = kReadingRequest;
			}
		}
	} 

	//���Sessionռ�õ�������Դ
	this->CleanupRequest();

	//Session������Ϊ0������-1��ϵͳ�Ὣ��Sessionɾ��
	if (fObjectHolders == 0)
		return -1;

	return 0;
}

QTSS_Error HTTPSession::SendHTTPPacket(StrPtrLen* contentXML, Bool16 connectionClose, Bool16 decrement)
{
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(httpOK);
	if (contentXML->Len)
		httpAck.AppendContentLengthHeader(contentXML->Len);

	if(connectionClose)
		httpAck.AppendConnectionCloseHeader();

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (contentXML->Len > 0) 
		pOutputStream->Put(contentXML->Ptr, contentXML->Len);

	if (pOutputStream->GetBytesWritten() != 0)
	{
		pOutputStream->Flush();
	}

	//����HTTPSession�����ü���һ
	if(fObjectHolders && decrement)
		DecrementObjectHolderCount();

	if(connectionClose)
		this->Signal(Task::kKillEvent);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::SetupRequest()
{
	//����������
	QTSS_Error theErr = fRequest->Parse();
	if (theErr != QTSS_NoErr)
		return QTSS_BadArgument;

	if (fRequest->GetRequestPath() != NULL)
	{
		string sRequest(fRequest->GetRequestPath());
		//�����Ƿ���Json���ֻ��Ƿ���HTTPͷ����
		//1.���ݷ���ͷ������ post http://****/123,��123Ϊ���ݲ��֣�����ʹ��post /123��123Ϊ���ݲ��֡�
		//2.���ݷ���JSON���֣�����post http://****/,ͷ�����������ݲ��֣���������JSON���ֵĴ���
		if (!sRequest.empty())
		{
			boost::to_lower(sRequest);

			vector<string> path;
			if (boost::ends_with(sRequest, "/"))
			{
				boost::erase_tail(sRequest, 1);
			}
			boost::split(path, sRequest, boost::is_any_of("/"), boost::token_compress_on);
			if (path.size() == 2)
			{
				if(path[0]=="api"&&path[1]=="getdevicelist")
				{
					ExecNetMsgCSGetDeviceListReqRESTful(fRequest->GetQueryString());//����豸�б�
					return 0;
				}
				else if(path[0]=="api"&&path[1]=="getdeviceinfo")
				{
					ExecNetMsgCSGetCameraListReqRESTful(fRequest->GetQueryString());//���ĳ���豸��ϸ��Ϣ
					return 0;
				}
				else if(path[0]=="api"&&path[1]=="getdevicestream")
				{
					ExecNetMsgCSGetStreamReqRESTful(fRequest->GetQueryString());//�ͻ��˵�ֱ������
					return 0;
				}
			}

			// ִ�е���Ķ�˵����Ҫ�����쳣����
			EasyMsgExceptionACK rsp;
			string msg = rsp.GetMsg();
			// ������Ӧ����(HTTP Header)
			HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
			httpAck.CreateResponseHeader(!msg.empty() ? httpOK : httpNotImplemented);
			if (!msg.empty())
				httpAck.AppendContentLengthHeader((UInt32)msg.length());

			httpAck.AppendConnectionCloseHeader();

			//Push HTTP Header to OutputBuffer
			char respHeader[2048] = { 0 };
			StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
			strncpy(respHeader, ackPtr->Ptr, ackPtr->Len);

			HTTPResponseStream *pOutputStream = GetOutputStream();
			pOutputStream->Put(respHeader);

			//Push HTTP Content to OutputBuffer
			if (!msg.empty())
				pOutputStream->Put((char*)msg.data(), msg.length());

			return QTSS_NoErr;
		}		
	}

	HTTPStatusCode statusCode = httpOK;
	char *body = NULL;
	UInt32 bodySizeBytes = 0;

	//��ȡ����Content json���ݲ���

	//1����ȡjson���ֳ���
	StrPtrLen* lengthPtr = fRequest->GetHeaderValue(httpContentLengthHeader);

	StringParser theContentLenParser(lengthPtr);
	theContentLenParser.ConsumeWhitespace();
	UInt32 content_length = theContentLenParser.ConsumeInteger(NULL);

	qtss_printf("HTTPSession read content-length:%d \n", content_length);

	if (content_length <= 0)
	{
		return QTSS_BadArgument;
	}

	// Check for the existence of 2 attributes in the request: a pointer to our buffer for
	// the request body, and the current offset in that buffer. If these attributes exist,
	// then we've already been here for this request. If they don't exist, add them.
	UInt32 theBufferOffset = 0;
	char* theRequestBody = NULL;
	UInt32 theLen = 0;
	theLen = sizeof(theRequestBody);
	theErr = QTSS_GetValue(this, EasyHTTPSesContentBody, 0, &theRequestBody, &theLen);

	if (theErr != QTSS_NoErr)
	{
		// First time we've been here for this request. Create a buffer for the content body and
		// shove it in the request.
		theRequestBody = NEW char[content_length + 1];
		memset(theRequestBody,0,content_length + 1);
		theLen = sizeof(theRequestBody);
		theErr = QTSS_SetValue(this, EasyHTTPSesContentBody, 0, &theRequestBody, theLen);// SetValue creates an internal copy.
		Assert(theErr == QTSS_NoErr);

		// Also store the offset in the buffer
		theLen = sizeof(theBufferOffset);
		theErr = QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, theLen);
		Assert(theErr == QTSS_NoErr);
	}

	theLen = sizeof(theBufferOffset);
	theErr = QTSS_GetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, &theLen);

	// We have our buffer and offset. Read the data.
	//theErr = QTSS_Read(this, theRequestBody + theBufferOffset, content_length - theBufferOffset, &theLen);
	theErr = fInputStream.Read(theRequestBody + theBufferOffset, content_length - theBufferOffset, &theLen);
	Assert(theErr != QTSS_BadArgument);

	if (theErr == QTSS_RequestFailed)
	{
		OSCharArrayDeleter charArrayPathDeleter(theRequestBody);
		//
		// NEED TO RETURN HTTP ERROR RESPONSE
		return QTSS_RequestFailed;
	}

	qtss_printf("HTTPSession read content-length:%d (%d/%d) \n", theLen, theBufferOffset+theLen, content_length);
	if ((theErr == QTSS_WouldBlock) || (theLen < ( content_length - theBufferOffset)))
	{
		//
		// Update our offset in the buffer
		theBufferOffset += theLen;
		(void)QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, sizeof(theBufferOffset));
		// The entire content body hasn't arrived yet. Request a read event and wait for it.

		Assert(theErr == QTSS_NoErr);
		return QTSS_WouldBlock;
	}
	//ִ�е���˵���Ѿ�������������HTTPHeader+JSON����
	fRequestBody = theRequestBody;//�����ݲ��ֱ�����������ProcessRequest����ȥ��������
	Assert(theErr == QTSS_NoErr);
	qtss_printf("Recv message: %s\n", fRequestBody);


	UInt32 offset = 0;
	(void)QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &offset, sizeof(offset));
	char* content = NULL;
	(void)QTSS_SetValue(this, EasyHTTPSesContentBody, 0, &content, 0);

	return QTSS_NoErr;
}

void HTTPSession::CleanupRequest()
{
	if (fRequest != NULL)
	{
		// NULL out any references to the current request
		delete fRequest;
		fRequest = NULL;
	}

	fSessionMutex.Unlock();
	fReadMutex.Unlock();

	// Clear out our last value for request body length before moving onto the next request
	this->SetRequestBodyLength(-1);
}

Bool16 HTTPSession::OverMaxConnections(UInt32 buffer)
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

QTSS_Error HTTPSession::DumpRequestData()
{
	char theDumpBuffer[EASY_REQUEST_BUFFER_SIZE_LEN];

	QTSS_Error theErr = QTSS_NoErr;
	while (theErr == QTSS_NoErr)
		theErr = this->Read(theDumpBuffer, EASY_REQUEST_BUFFER_SIZE_LEN, NULL);

	return theErr;
}

QTSS_Error HTTPSession::ExecNetMsgDSPostSnapReq(const char* json)//�豸��������
{
	if(!fAuthenticated) return httpUnAuthorized;

	EasyDarwin::Protocol::EasyMsgDSPostSnapREQ parse(json);

	string image			=	parse.GetBodyValue(EASY_TAG_IMAGE);	
	string channel			=	parse.GetBodyValue(EASY_TAG_CHANNEL);
	string device_serial	=	parse.GetBodyValue(EASY_TAG_SERIAL);
	string strType			=	parse.GetBodyValue(EASY_TAG_TYPE);//���;���ͼƬ����չ��
	string strTime			=	parse.GetBodyValue(EASY_TAG_TIME);//ʱ������

	if(channel.empty())//Ϊ��ѡ�����Ĭ��ֵ
		channel = "01";
	if(strTime.empty())//���û��ʱ�����ԣ��������Զ�Ϊ������һ��
		strTime = EasyUtil::NowTime(EASY_TIME_FORMAT_YYYYMMDDHHMMSSEx);
		
	if( (image.size() <= 0) || (device_serial.size() <= 0) || (strType.size() <= 0) || (strTime.size() <= 0) )
		return QTSS_BadArgument;

	//�ȶ����ݽ���Base64����
	image = EasyUtil::Base64Decode(image.data(), image.size());

	//�ļ���·�����ɿ���·��+Serial�ϳ�
	char jpgDir[512] = { 0 };
	qtss_sprintf(jpgDir,"%s%s", QTSServerInterface::GetServer()->GetPrefs()->GetSnapLocalPath() ,device_serial.c_str());
	OS::RecursiveMakeDir(jpgDir);

	char jpgPath[512] = { 0 };

	//�ļ�ȫ·�����ļ�����Serial_Channel_Time.Type�ϳ�
	qtss_sprintf(jpgPath,"%s/%s_%s_%s.%s", jpgDir, device_serial.c_str(), channel.c_str(),strTime.c_str(),strType.c_str());

	//�����������
	FILE* fSnap = ::fopen(jpgPath, "wb");
	if(fSnap==NULL)//����ԭ���кܶ࣬������չ�����淶���ļ������淶,���ļ��Ѿ��������ļ���
	{
		//DWORD e=GetLastError();
		return QTSS_NoErr;
	}
	fwrite(image.data(), 1, image.size(), fSnap);
	::fclose(fSnap);
	
	char snapURL[512] = { 0 };
	qtss_sprintf(snapURL, "%s/%s/%s_%s.%s",QTSServerInterface::GetServer()->GetPrefs()->GetSnapWebPath(), device_serial.c_str(), device_serial.c_str(),channel.c_str(),strType.c_str());
	fDevice.HoldSnapPath(jpgPath,channel);


	EasyProtocolACK rsp(MSG_SD_POST_SNAP_ACK);
	EasyJsonValue header,body;

	header[EASY_TAG_VERSION]		=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]			=	parse.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM]		=	EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING]	=	EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_SERIAL]			=	device_serial;
	body[EASY_TAG_CHANNEL]			=	channel;

	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTP Header)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);

	//����Ӧ������ӵ�HTTP�����������
	if (!msg.empty()) 
		pOutputStream->Put((char*)msg.data(), msg.length());
	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgErrorReqHandler(HTTPStatusCode errCode)
{
	//������Ӧ����(HTTP Header)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(errCode);

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);

	return QTSS_NoErr;
}

/*
	1.��ȡTerminalType��AppType,�����߼���֤���������򷵻�400 httpBadRequest;
	2.��֤Serial��Token����Ȩ����֤���������򷵻�401 httpUnAuthorized;
	3.��ȡName��Tag��Ϣ���б��ر������д��Redis;
	4.�����APPTypeΪEasyNVR,��ȡChannelsͨ����Ϣ���ر������д��Redis
*/
QTSS_Error HTTPSession::ExecNetMsgDSRegisterReq(const char* json)
{
	QTSS_Error theErr = QTSS_NoErr;
	EasyMsgDSRegisterREQ regREQ(json);

	while(!fAuthenticated)
	{
		//1.��ȡTerminalType��AppType,�����߼���֤���������򷵻�400 httpBadRequest;
		int appType = regREQ.GetAppType();
		int terminalType = regREQ.GetTerminalType();
		switch(appType)
		{
		case EASY_APP_TYPE_CAMERA:
			{
				fSessionType = EasyCameraSession;
				fTerminalType = terminalType;
				break;
			}
		case EASY_APP_TYPE_NVR:
			{
				fSessionType = EasyNVRSession;
				fTerminalType = terminalType;
				break;
			}
		default:
			{
				break;
			}
		}

		if(fSessionType >= EasyHTTPSession)
		{
			//�豸ע��Ȳ���EasyCamera��Ҳ����EasyNVR�����ش���
			theErr = QTSS_BadArgument;
			break;
		}

		//2.��֤Serial��Token����Ȩ����֤���������򷵻�401 httpUnAuthorized;
		string serial = regREQ.GetBodyValue(EASY_TAG_SERIAL);
		string token = regREQ.GetBodyValue(EASY_TAG_TOKEN);

		if(serial.empty())
		{
			theErr = QTSS_AttrDoesntExist;
			break;
		}

		//��֤Serial��Token�Ƿ�Ϸ�
		if(false)
		{
			theErr = QTSS_NotPreemptiveSafe;
			break;
		}

		//3.��ȡName��Tag��Ϣ���б��ر������д��Redis;
		if(!fDevice.GetDevInfo(json))
		{
			theErr = QTSS_BadArgument;
			break;
		}

		OS_Error regErr = QTSServerInterface::GetServer()->GetDeviceSessionMap()->Register(fDevice.serial_,this);
		if(regErr == OS_NoErr)
		{
			//��redis�������豸
			QTSServerInterface::GetServer()->RedisAddDevName(fDevice.serial_.c_str());
			fAuthenticated = true;
		}
		else
		{
			//���߳�ͻ
			theErr =  QTSS_AttrNameExists;
			break;
		}

		break;
	}

	if(theErr != QTSS_NoErr)	return theErr;

	//�ߵ���˵�����豸�ɹ�ע���������
	EasyProtocol req(json);
	EasyProtocolACK rsp(MSG_SD_REGISTER_ACK);
	EasyJsonValue header,body;
	header[EASY_TAG_VERSION]=EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]=req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM]=200;
	header[EASY_TAG_ERROR_STRING]=EasyProtocol::GetErrorString(200);

	body[EASY_TAG_SERIAL]=fDevice.serial_;
	body[EASY_TAG_SESSION_ID]=fSessionID;

	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTP Header)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);

	//����Ӧ������ӵ�HTTP�����������
	if (!msg.empty()) 
		pOutputStream->Put((char*)msg.data(), msg.length());

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSFreeStreamReq(const char* json)//�ͻ��˵�ֱֹͣ������
{
	//�㷨����������ָ���豸�����豸���ڣ�ɾ��set�еĵ�ǰ�ͻ���Ԫ�ز��ж�set�Ƿ�Ϊ�գ�Ϊ�������豸����ֹͣ������
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	EasyDarwin::Protocol::EasyProtocol req(json);
	string strCSeq=req.GetHeaderValue(EASY_TAG_CSEQ);
	UInt32 uCSeq=atoi(strCSeq.c_str());

	string strDeviceSerial	=	req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	string strCameraSerial	=	req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	string strStreamID		=   req.GetBodyValue(EASY_TAG_RESERVE);//StreamID
	string strProtocol		=	req.GetBodyValue(EASY_TAG_PROTOCOL);//Protocol

	if(strDeviceSerial.size()<=0||strCameraSerial.size()<=0||strStreamID.size()<=0||strProtocol.size()<=0)//�����ж�
		return QTSS_BadArgument;

	OSRefTableEx* DeviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = DeviceMap->Resolve(strDeviceSerial);////////////////////////////////++
	if(theDevRef==NULL)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	HTTPSession * pDevSession = (HTTPSession *)theDevRef->GetObjectPtr();//��õ�ǰ�豸�ػ�
	stStreamInfo  stTemp;
	if(!FindInStreamMap(strDeviceSerial+strCameraSerial,stTemp))//û�жԵ�ǰ�豸����ֱ��,�еĻ���ɾ���Ը�����ͷ�ļ�¼
	{
		DeviceMap->Release(strDeviceSerial);//////////////////////////////////////////////////////////--
		return QTSS_BadArgument;
	} 
	if(pDevSession->EraseInSet(strCameraSerial,this))//����豸�Ŀͻ����б�Ϊ�գ������豸����ֹͣ��������
	{
		EasyDarwin::Protocol::EasyProtocolACK		reqreq(MSG_SD_STREAM_STOP_REQ);
		EasyJsonValue headerheader,bodybody;

		char chTemp[16] = {0};
		UInt32 uDevCseq = pDevSession->GetCSeq();
		sprintf(chTemp,"%d",uDevCseq);
		headerheader[EASY_TAG_CSEQ]			=	string(chTemp);//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
		headerheader[EASY_TAG_VERSION]		=	EASY_PROTOCOL_VERSION;

		bodybody[EASY_TAG_SERIAL]			=	strDeviceSerial;
		bodybody[EASY_TAG_CHANNEL]			=	strCameraSerial;
		bodybody[EASY_TAG_RESERVE]			=   strStreamID;
		bodybody[EASY_TAG_PROTOCOL]			=	strProtocol;

		reqreq.SetHead(headerheader);
		reqreq.SetBody(bodybody);

		string buffer = reqreq.GetMsg();
		//QTSS_SendHTTPPacket(pDevSession,(char*)buffer.c_str(),buffer.size(),false,false);
	}
	else//˵����Ȼ�������ͻ������ڶԵ�ǰ����ͷ����ֱ��
	{
		
	}
	DeviceMap->Release(strDeviceSerial);//////////////////////////////////////////////////////////--
	//�ͻ��˲������Ķ���ֱֹͣ���Ļ�Ӧ����˶Կͻ��˵�ֱֹͣ�������л�Ӧ
	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgDSStreamStopAck(const char* json)//�豸��ֹͣ������Ӧ
{
	if(!fAuthenticated)//û�н�����֤����
		return httpUnAuthorized;

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSGetStreamReqRESTful(char *queryString)//�ŵ�ProcessRequest���ڵ�״̬ȥ����������ѭ������
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/
	QueryParamList parList(queryString);
	const char* chSerial	=	parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�
	const char* chChannel	=	parList.DoFindCGIValueForParam(EASY_TAG_L_CHANNEL);//��ȡͨ��
	const char* chProtocol  =   parList.DoFindCGIValueForParam(EASY_TAG_L_PROTOCOL);//��ȡͨ��
	const char* chReserve	=	parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);//��ȡͨ��

	//Ϊ��ѡ�������Ĭ��ֵ
	if(chChannel==NULL)
		chChannel = "01";
	if(chReserve==NULL)
		chReserve = "1";

	if(chSerial==NULL||chProtocol==NULL)
		return QTSS_BadArgument;

	EasyDarwin::Protocol::EasyProtocolACK req(MSG_CS_GET_STREAM_REQ);//��restful�ӿںϳ�json��ʽ����
	EasyJsonValue header,body;

	char chTemp[16] = {0};//����ͻ��˲��ṩCSeq,��ô����ÿ�θ�������һ��Ψһ��CSeq
	UInt32 uCseq = GetCSeq();
	sprintf(chTemp,"%d",uCseq);

	header[EASY_TAG_CSEQ]		=		chTemp;
	header[EASY_TAG_VERSION]	=		"1.0";
	body[EASY_TAG_SERIAL]		=		chSerial;
	body[EASY_TAG_CHANNEL]		=		chChannel;
	body[EASY_TAG_PROTOCOL]		=		chProtocol;
	body[EASY_TAG_RESERVE]		=		chReserve;

	req.SetHead(header);
	req.SetBody(body);

	string buffer = req.GetMsg();
	fRequestBody  = new char[buffer.size()+1];
	strcpy(fRequestBody,buffer.c_str());
	return QTSS_NoErr;

}

QTSS_Error HTTPSession::ExecNetMsgCSGetStreamReq(const char* json)//�ͻ��˿�ʼ������
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	EasyDarwin::Protocol::EasyProtocol req(json);
	string strCSeq = req.GetHeaderValue(EASY_TAG_CSEQ);
	UInt32 uCSeq = atoi(strCSeq.c_str());
	string strURL;//ֱ����ַ

	string strDeviceSerial	=	req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	string strCameraSerial	=	req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	string strProtocol		=	req.GetBodyValue(EASY_TAG_PROTOCOL);//Э��
	string strStreamID		=	req.GetBodyValue(EASY_TAG_RESERVE);//������

	//��ѡ�������û�У�����Ĭ��ֵ���
	if(strCameraSerial.empty())//��ʾΪEasyCamera�豸
		strCameraSerial = "01";
	if(strStreamID.empty())//û����������ʱĬ��Ϊ����
		strStreamID = "1";

	if(strDeviceSerial.size()<=0||strProtocol.size()<=0)//�����ж�
		return QTSS_BadArgument;

	if(fInfo.cWaitingState==0)//��һ�δ�������
	{
		OSRefTableEx* DeviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
		OSRefTableEx::OSRefEx* theDevRef = DeviceMap->Resolve(strDeviceSerial);////////////////////////////////++
		if(theDevRef==NULL)//�Ҳ���ָ���豸
			return EASY_ERROR_DEVICE_NOT_FOUND;

		//�ߵ���˵������ָ���豸
		HTTPSession * pDevSession = (HTTPSession *)theDevRef->GetObjectPtr();//��õ�ǰ�豸�ػ�
		string strDssIP,strDssPort;
		if(QTSServerInterface::GetServer()->RedisGetAssociatedDarWin(strDeviceSerial,strCameraSerial,strDssIP,strDssPort))//�Ƿ���ڹ�����EasyDarWinת��������test,Ӧ����Redis�ϵ����ݣ���Ϊ�����ǲ��ɿ��ģ���EasyDarWin�ϵ������ǿɿ���
		{
			//�ϳ�ֱ����RTSP��ַ�������п��ܸ�����������Э�鲻ͬ�����ɲ�ͬ��ֱ����ַ����RTMP��HLS��
			string strSessionID;
			bool bReval = QTSServerInterface::GetServer()->RedisGenSession(strSessionID,SessionIDTimeout);
			if(!bReval)//sessionID��redis�ϵĴ洢ʧ��
			{
				DeviceMap->Release(strDeviceSerial);/////////////////////////////////////////////--
				return EASY_ERROR_SERVER_INTERNAL_ERROR;
			}
			strURL="rtsp://"+strDssIP+':'+strDssPort+'/'
				+strSessionID+'/'
				+strDeviceSerial+'/'
				+strCameraSerial+".sdp";

			pDevSession->InsertToSet(strCameraSerial,this);//����ǰ�ͻ��˼��뵽�����ͻ����б���
			stStreamInfo stTemp;
			stTemp.strDeviceSerial = strDeviceSerial;
			stTemp.strCameraSerial = strCameraSerial;
			stTemp.strProtocol = strProtocol;
			stTemp.strStreamID = strStreamID;
			InsertToStreamMap(strDeviceSerial+strCameraSerial,stTemp);//���豸���к�+����ͷ���к���ΪΨһ��ʶ
			//�����Ѿ��ò����豸�ػ��ˣ��ͷ�����
			DeviceMap->Release(strDeviceSerial);/////////////////////////////////////////////--
		}
		else
		{//�����ڹ�����EasyDarWin
			bool bErr = QTSServerInterface::GetServer()->RedisGetBestDarWin(strDssIP,strDssPort);
			if(!bErr)//������DarWin
			{
				DeviceMap->Release(strDeviceSerial);/////////////////////////////////////////////--
				return EASY_ERROR_SERVICE_NOT_FOUND;
			}
			//��ָ���豸���Ϳ�ʼ������

			EasyDarwin::Protocol::EasyProtocolACK		reqreq(MSG_SD_PUSH_STREAM_REQ);
			EasyJsonValue headerheader,bodybody;

			char chTemp[16]={0};
			UInt32 uDevCseq=pDevSession->GetCSeq();
			sprintf(chTemp,"%d",uDevCseq);
			headerheader[EASY_TAG_CSEQ] = string(chTemp);//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
			headerheader[EASY_TAG_VERSION]=		EASY_PROTOCOL_VERSION;

			string strSessionID;
			bool bReval = QTSServerInterface::GetServer()->RedisGenSession(strSessionID,SessionIDTimeout);
			if(!bReval)//sessionID��redis�ϵĴ洢ʧ��
			{
				DeviceMap->Release(strDeviceSerial);/////////////////////////////////////////////--
				return EASY_ERROR_SERVER_INTERNAL_ERROR;
			}

			bodybody[EASY_TAG_STREAM_ID]		=		strSessionID;
			bodybody[EASY_TAG_SERVER_IP]		=		strDssIP;
			bodybody[EASY_TAG_SERVER_PORT]		=		strDssPort;
			bodybody[EASY_TAG_SERIAL]			=		strDeviceSerial;
			bodybody[EASY_TAG_CHANNEL]			=		strCameraSerial;
			bodybody[EASY_TAG_PROTOCOL]			=		strProtocol;
			bodybody[EASY_TAG_RESERVE]			=		strStreamID;

			reqreq.SetHead(headerheader);
			reqreq.SetBody(bodybody);

			string buffer = reqreq.GetMsg();
			//
			strMessage msgTemp;

			msgTemp.iMsgType = MSG_CS_GET_STREAM_REQ;//��ǰ�������Ϣ
			msgTemp.pObject = this;//��ǰ����ָ��
			msgTemp.uCseq = uCSeq;//��ǰ�����cseq

			pDevSession->InsertToMsgMap(uDevCseq,msgTemp);//���뵽Map�еȴ��ͻ��˵Ļ�Ӧ
			IncrementObjectHolderCount();//�������ã���ֹ�豸��Ӧʱ��ǰSession�Ѿ���ֹ
			//QTSS_SendHTTPPacket(pDevSession,(char*)buffer.c_str(),buffer.size(),false,false);
			DeviceMap->Release(strDeviceSerial);//////////////////////////////////////////////////////////--

			fInfo.cWaitingState = 1;//�ȴ��豸��Ӧ
			fInfo.iResponse = 0;//��ʾ�豸��û�л�Ӧ
			fInfo.uTimeoutNum = 0;//��ʼ���㳬ʱ
			fInfo.uWaitingTime = 100;//��100msΪ����ѭ���ȴ���������ռ��CPU
			return QTSS_NoErr;
		}
	}
	else//�ȴ��豸��Ӧ 
	{
		if(fInfo.iResponse==0)//�豸��û�л�Ӧ
		{
			fInfo.uTimeoutNum++;
			if(fInfo.uTimeoutNum>CliStartStreamTimeout/100)//��ʱ��
			{
				fInfo.cWaitingState = 0;//�ָ�״̬
				return httpRequestTimeout;
			}
			else//û�г�ʱ�������ȴ�
			{
				fInfo.uWaitingTime = 100;//��100msΪ����ѭ���ȴ���������ռ��CPU
				return QTSS_NoErr;
			}
		}
		else if(fInfo.uCseq!=uCSeq)//�����������Ҫ�ģ������ǵ�һ������ʱ��ʱ���ڶ�������ʱ�����˵�һ���Ļ�Ӧ����ʱ����Ӧ�ü����ȴ��ڶ����Ļ�Ӧֱ����ʱ
		{
			fInfo.iResponse = 0;//�����ȴ�����һ�����ܺ���һ���߳�ͬʱ��ֵ������Ҳ���ܽ����ֻ����Ӱ�첻��
			fInfo.uTimeoutNum++;
			fInfo.uWaitingTime = 100;//��100msΪ����ѭ���ȴ���������ռ��CPU
			return QTSS_NoErr;
		}
		else if(fInfo.iResponse==200)//��ȷ��Ӧ
		{
			fInfo.cWaitingState = 0;//�ָ�״̬
			strStreamID = fInfo.strStreamID;//ʹ���豸�������ͺ�����Э��
			strProtocol = fInfo.strProtocol;
			//�ϳ�ֱ����ַ

			string strSessionID;
			bool bReval=QTSServerInterface::GetServer()->RedisGenSession(strSessionID,SessionIDTimeout);
			if(!bReval)//sessionID��redis�ϵĴ洢ʧ��
			{
				return EASY_ERROR_SERVER_INTERNAL_ERROR;
			}

			strURL = "rtsp://"+fInfo.strDssIP+':'+fInfo.strDssPort+'/'
				+strSessionID+'/'
				+strDeviceSerial+'/'
				+strCameraSerial+".sdp";

			//�Զ�ֹͣ����add
			OSRefTableEx* DeviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
			OSRefTableEx::OSRefEx* theDevRef = DeviceMap->Resolve(strDeviceSerial);////////////////////////////////++
			if(theDevRef==NULL)//�Ҳ���ָ���豸
				return EASY_ERROR_DEVICE_NOT_FOUND;
			//�ߵ���˵������ָ���豸
			HTTPSession * pDevSession = (HTTPSession *)theDevRef->GetObjectPtr();//��õ�ǰ�豸�ػ�
			pDevSession->InsertToSet(strCameraSerial,this);//����ǰ�ͻ��˼��뵽�����ͻ����б���
			stStreamInfo stTemp;
			stTemp.strDeviceSerial = strDeviceSerial;
			stTemp.strCameraSerial = strCameraSerial;
			stTemp.strProtocol = strProtocol;
			stTemp.strStreamID = strStreamID;
			InsertToStreamMap(strDeviceSerial+strCameraSerial,stTemp);
			DeviceMap->Release(strDeviceSerial);////////////////////////////////////////////////--
			//�Զ�ֹͣ����add
		}
		else//�豸�����Ӧ
		{
			fInfo.cWaitingState = 0;//�ָ�״̬
			return fInfo.iResponse;
		}
	}

	//�ߵ���˵���Կͻ��˵���ȷ��Ӧ,��Ϊ�����Ӧֱ�ӷ��ء�
	EasyDarwin::Protocol::EasyProtocolACK rsp(MSG_SC_GET_STREAM_ACK);
	EasyJsonValue header,body;
	body[EASY_TAG_URL]			=	strURL;
	body[EASY_TAG_SERIAL]		=	strDeviceSerial;
	body[EASY_TAG_CHANNEL]		=	strCameraSerial;
	body[EASY_TAG_PROTOCOL]		=	strProtocol;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������
	body[EASY_TAG_RESERVE]		=	strStreamID;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������

	header[EASY_TAG_VERSION]	=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]		=	strCSeq;
	header[EASY_TAG_ERROR_NUM]	=	200;
	header[EASY_TAG_ERROR_STRING] = EasyDarwin::Protocol::EasyProtocol::GetErrorString(200);


	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTP Header)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);

	//����Ӧ������ӵ�HTTP�����������
	if (!msg.empty()) 
		pOutputStream->Put((char*)msg.data(), msg.length());
	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgDSPushStreamAck(const char* json)//�豸�Ŀ�ʼ����Ӧ
{
	if(!fAuthenticated)//û�н�����֤����
		return httpUnAuthorized;

	//�����豸��������Ӧ�ǲ���Ҫ�ڽ��л�Ӧ�ģ�ֱ�ӽ����ҵ���Ӧ�Ŀͻ���Session����ֵ����	
	EasyDarwin::Protocol::EasyProtocol req(json);


	string strDeviceSerial	=	req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	string strCameraSerial	=	req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	//string strProtocol		=	req.GetBodyValue("Protocol");//Э��,�ն˽�֧��RTSP����
	string strStreamID		=	req.GetBodyValue(EASY_TAG_RESERVE);//������
	string strDssIP         =   req.GetBodyValue(EASY_TAG_SERVER_IP);//�豸ʵ��������ַ
	string strDssPort       =   req.GetBodyValue(EASY_TAG_SERVER_PORT);//�Ͷ˿�

	string strCSeq			=	req.GetHeaderValue(EASY_TAG_CSEQ);//����ǹؼ���
	string strStateCode     =   req.GetHeaderValue(EASY_TAG_ERROR_NUM);//״̬��

	UInt32 uCSeq = atoi(strCSeq.c_str());
	int iStateCode = atoi(strStateCode.c_str());

	strMessage strTempMsg;
	if(!FindInMsgMap(uCSeq,strTempMsg))
	{//�찡����Ȼ�Ҳ�����һ�����豸���͵�CSeq�������յĲ�һ��
		return QTSS_BadArgument;
	}
	else
	{
		HTTPSession * pCliSession = (HTTPSession *)strTempMsg.pObject;//�������ָ������Ч�ģ���Ϊ֮ǰ���Ǹ������˻�����
		if(strTempMsg.iMsgType==MSG_CS_GET_STREAM_REQ)//�ͻ��˵Ŀ�ʼ������
		{
			if(iStateCode==200)//ֻ����ȷ��Ӧ�Ž���һЩ��Ϣ�ı���
			{
				pCliSession->fInfo.strDssIP = strDssIP;
				pCliSession->fInfo.strDssPort = strDssPort;
				pCliSession->fInfo.strStreamID = strStreamID;
				//pCliSession->fInfo.strProtocol=strProtocol;
			}
			pCliSession->fInfo.uCseq = strTempMsg.uCseq;
			pCliSession->fInfo.iResponse = iStateCode;//���֮��ʼ�����ͻ��˶���
			pCliSession->DecrementObjectHolderCount();//���ڿ��Է��ĵİ�Ϣ��
		}
		else
		{

		}
	}
	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSGetDeviceListReqRESTful(char *queryString)//�ͻ��˻���豸�б�
{
	//queryString�������������û���õģ���Ϊ�˱��ֽӿڵ�һ���ԡ�
	/*
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	EasyDarwin::Protocol::EasyProtocolACK		rsp(MSG_SC_DEVICE_LIST_ACK);
	EasyJsonValue header,body;

	header[EASY_TAG_VERSION]		=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]			=	1;
	header[EASY_TAG_ERROR_NUM]		=	200;
	header[EASY_TAG_ERROR_STRING]	=	EasyDarwin::Protocol::EasyProtocol::GetErrorString(200);


	OSMutex *mutexMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMutex();
	OSHashMap  *deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMap();
	OSRefIt itRef;
	Json::Value *proot = rsp.GetRoot();

	mutexMap->Lock();
	body[EASY_TAG_DEVICE_COUNT] = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetEleNumInMap();
	for(itRef=deviceMap->begin();itRef!=deviceMap->end();itRef++)
	{
		Json::Value value;
		strDevice *deviceInfo		= ((HTTPSession*)(itRef->second->GetObjectPtr()))->GetDeviceInfo();
		value[EASY_TAG_SERIAL]		=	deviceInfo->serial_;
		value[EASY_TAG_NAME]		=	deviceInfo->name_;
		value[EASY_TAG_TAG]			=	deviceInfo->tag_;
		value[EASY_TAG_APP_TYPE]	=	EasyProtocol::GetAppTypeString(deviceInfo->eAppType);
		value[EASY_TAG_TERMINAL_TYPE]	=	EasyProtocol::GetTerminalTypeString(deviceInfo->eDeviceType);
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_DEVICES].append(value);
	}
	mutexMap->Unlock();

	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTPͷ)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	//��Ӧ��ɺ�Ͽ�����
	httpAck.AppendConnectionCloseHeader();

	//Push MSG to OutputBuffer
	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (!msg.empty())
		pOutputStream->Put((char*)msg.data(), msg.length());

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSDeviceListReq(const char *json)//�ͻ��˻���豸�б�
{
	/*
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/
	EasyDarwin::Protocol::EasyProtocol		req(json);


	EasyDarwin::Protocol::EasyProtocolACK		rsp(MSG_SC_DEVICE_LIST_ACK);
	EasyJsonValue header,body;

	header[EASY_TAG_VERSION]		=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]			=	req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM]		=	200;
	header[EASY_TAG_ERROR_STRING]	=	EasyDarwin::Protocol::EasyProtocol::GetErrorString(200);


	OSMutex *mutexMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMutex();
	OSHashMap  *deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMap();
	OSRefIt itRef;
	Json::Value *proot = rsp.GetRoot();

	mutexMap->Lock();
	body[EASY_TAG_DEVICE_COUNT] = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetEleNumInMap();
	for(itRef=deviceMap->begin();itRef!=deviceMap->end();itRef++)
	{
		Json::Value value;
		strDevice *deviceInfo=((HTTPSession*)(itRef->second->GetObjectPtr()))->GetDeviceInfo();
		value[EASY_TAG_SERIAL]			=	deviceInfo->serial_;
		value[EASY_TAG_NAME]			=	deviceInfo->name_;
		value[EASY_TAG_TAG]				=	deviceInfo->tag_;
		value[EASY_TAG_APP_TYPE]		=	EasyProtocol::GetAppTypeString(deviceInfo->eAppType);
		value[EASY_TAG_TERMINAL_TYPE]	=	EasyProtocol::GetTerminalTypeString(deviceInfo->eDeviceType);
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_DEVICES].append(value);
	}
	mutexMap->Unlock();

	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTPͷ)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	//Push MSG to OutputBuffer
	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (!msg.empty())
		pOutputStream->Put((char*)msg.data(), msg.length());

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSGetCameraListReqRESTful(char* queryString)
{
	/*	
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	QueryParamList parList(queryString);
	const char* device_serial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�

	if(device_serial==NULL)
		return QTSS_BadArgument;

	EasyDarwin::Protocol::EasyProtocolACK		rsp(MSG_SC_DEVICE_INFO_ACK);
	EasyJsonValue header,body;

	header[EASY_TAG_VERSION]		= EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]		    = 1;

	body[EASY_TAG_SERIAL]			= device_serial;

	OSRefTableEx* DeviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = DeviceMap->Resolve(device_serial);////////////////////////////////++
	if(theDevRef==NULL)//������ָ���豸
	{
		header[EASY_TAG_ERROR_NUM]    = 603;
		header[EASY_TAG_ERROR_STRING] = EasyDarwin::Protocol::EasyProtocol::GetErrorString(EASY_ERROR_DEVICE_NOT_FOUND);
	}
	else//����ָ���豸�����ȡ����豸������ͷ��Ϣ
	{
		header[EASY_TAG_ERROR_NUM]    = 200;
		header[EASY_TAG_ERROR_STRING] = EasyDarwin::Protocol::EasyProtocol::GetErrorString(200);

		Json::Value *proot=rsp.GetRoot();
		strDevice *deviceInfo= ((HTTPSession*)theDevRef->GetObjectPtr())->GetDeviceInfo();
		if(deviceInfo->eAppType==EASY_APP_TYPE_CAMERA)
		{
			body[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
		}
		else
		{
			EasyDevices *camerasInfo = &(deviceInfo->channels_);
			EasyDevicesIterator itCam;
			body[EASY_TAG_CHANNEL_COUNT] = ((HTTPSession*)theDevRef->GetObjectPtr())->GetDeviceInfo()->channelCount_;
			for(itCam=camerasInfo->begin();itCam!=camerasInfo->end();itCam++)
			{
				Json::Value value;
				value[EASY_TAG_CHANNEL]  = itCam->channel_;
				value[EASY_TAG_NAME]	 = itCam->name_;
				value[EASY_TAG_STATUS]	 = itCam->status_;
				value[EASY_TAG_SNAP_URL] = itCam->snapJpgPath_;
				(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_CHANNELS].append(value);
			}
		}
		DeviceMap->Release(device_serial);////////////////////////////////--
	}
	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTPͷ)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	//��Ӧ��ɺ�Ͽ�����
	httpAck.AppendConnectionCloseHeader();

	//Push MSG to OutputBuffer
	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (!msg.empty())
		pOutputStream->Put((char*)msg.data(), msg.length());

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ExecNetMsgCSCameraListReq(const char* json)
{
	/*	
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	EasyDarwin::Protocol::EasyProtocol      req(json);
	string strDeviceSerial	=	req.GetBodyValue(EASY_TAG_SERIAL);

	if(strDeviceSerial.size()<=0)
		return QTSS_BadArgument;

	EasyDarwin::Protocol::EasyProtocolACK		rsp(MSG_SC_DEVICE_INFO_ACK);
	EasyJsonValue header,body;

	header[EASY_TAG_VERSION]		=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ]			=	req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM]		=	200;
	header[EASY_TAG_ERROR_STRING]	=	EasyDarwin::Protocol::EasyProtocol::GetErrorString(200);
	body[EASY_TAG_SERIAL]			=	strDeviceSerial;

	OSRefTableEx* DeviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = DeviceMap->Resolve(strDeviceSerial);////////////////////////////////++
	if(theDevRef==NULL)//������ָ���豸
	{
		return EASY_ERROR_DEVICE_NOT_FOUND;//�������������Ĵ���
	}
	else//����ָ���豸�����ȡ����豸������ͷ��Ϣ
	{

		Json::Value *proot = rsp.GetRoot();
		strDevice *deviceInfo =  ((HTTPSession*)theDevRef->GetObjectPtr())->GetDeviceInfo();
		if(deviceInfo->eAppType==EASY_APP_TYPE_CAMERA)
		{
			body[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
		}
		else
		{
			EasyDevices *camerasInfo = &(deviceInfo->channels_);
			EasyDevicesIterator itCam;

			body[EASY_TAG_CHANNEL_COUNT] = ((HTTPSession*)theDevRef->GetObjectPtr())->GetDeviceInfo()->channelCount_;
			for(itCam=camerasInfo->begin();itCam!=camerasInfo->end();itCam++)
			{
				Json::Value value;
				value[EASY_TAG_CHANNEL]		=	itCam->channel_;
				value[EASY_TAG_NAME]		=	itCam->name_;
				value[EASY_TAG_STATUS]		=	itCam->status_;
				body[EASY_TAG_SNAP_URL]		=	itCam->snapJpgPath_;
				(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_CHANNELS].append(value);
			}
		}
		DeviceMap->Release(strDeviceSerial);////////////////////////////////--
	}
	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();

	//������Ӧ����(HTTPͷ)
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
	if (!msg.empty())
		httpAck.AppendContentLengthHeader((UInt32)msg.length());

	//��Ӧ��ɺ�Ͽ�����
	httpAck.AppendConnectionCloseHeader();

	//Push MSG to OutputBuffer
	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

	HTTPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (!msg.empty())
		pOutputStream->Put((char*)msg.data(), msg.length());

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::ProcessRequest()//��������
{
	//OSCharArrayDeleter charArrayPathDeleter(theRequestBody);//��Ҫ����ɾ������Ϊ����ִ�ж�Σ�����������Ĵ�����Ϻ��ٽ���ɾ��

	if(fRequestBody == NULL)//��ʾû����ȷ�Ľ�������SetUpRequest����û�н��������ݲ���
		return QTSS_NoErr;

	//��Ϣ����
	QTSS_Error theErr = QTSS_NoErr;

	EasyDarwin::Protocol::EasyProtocol protocol(fRequestBody);
	int nNetMsg = protocol.GetMessageType(),nRspMsg = MSG_SC_EXCEPTION;

	switch (nNetMsg)
	{
	case MSG_DS_REGISTER_REQ://�����豸������Ϣ,�豸���Ͱ���NVR������ͷ����������
		theErr = ExecNetMsgDSRegisterReq(fRequestBody);
		nRspMsg = MSG_SD_REGISTER_ACK;
		break;
	case MSG_CS_GET_STREAM_REQ://�ͻ��˵Ŀ�ʼ������
		theErr = ExecNetMsgCSGetStreamReq(fRequestBody);
		nRspMsg = MSG_SC_GET_STREAM_ACK;
		break;
	case MSG_DS_PUSH_STREAM_ACK://�豸�Ŀ�ʼ����Ӧ
		theErr = ExecNetMsgDSPushStreamAck(fRequestBody);
		nRspMsg = MSG_DS_PUSH_STREAM_ACK;//ע�⣬����ʵ�����ǲ�Ӧ���ٻ�Ӧ��
		break;
	case MSG_CS_FREE_STREAM_REQ://�ͻ��˵�ֱֹͣ������
		theErr = ExecNetMsgCSFreeStreamReq(fRequestBody);
		nRspMsg = MSG_SC_FREE_STREAM_ACK;
		break;
	case MSG_DS_STREAM_STOP_ACK://�豸��EasyCMS��ֹͣ������Ӧ
		theErr = ExecNetMsgDSStreamStopAck(fRequestBody);
		nRspMsg = MSG_DS_STREAM_STOP_ACK;//ע�⣬����ʵ�����ǲ���Ҫ�ڽ��л�Ӧ��
		break;
	case MSG_CS_DEVICE_LIST_REQ://�豸�б�����
		theErr = ExecNetMsgCSDeviceListReq(fRequestBody);//add
		nRspMsg=MSG_SC_DEVICE_LIST_ACK;
		break;
	case MSG_CS_DEVICE_INFO_REQ://����ͷ�б�����,�豸�ľ�����Ϣ
		theErr = ExecNetMsgCSCameraListReq(fRequestBody);//add
		nRspMsg = MSG_SC_DEVICE_INFO_ACK;
		break;
	case MSG_DS_POST_SNAP_REQ://�豸�����ϴ�
		theErr = ExecNetMsgDSPostSnapReq(fRequestBody);
		nRspMsg = MSG_SD_POST_SNAP_ACK;
		break;
	default:
		theErr = ExecNetMsgErrorReqHandler(httpNotImplemented);
		break;
	}

	//��������������Զ�������һ��Ҫ����QTSS_NoErr
	if(theErr != QTSS_NoErr)//��������ȷ��Ӧ���ǵȴ����ض���QTSS_NoErr�����ִ��󣬶Դ������ͳһ��Ӧ
	{
		EasyDarwin::Protocol::EasyProtocol req(fRequestBody);
		EasyDarwin::Protocol::EasyProtocolACK rsp(nRspMsg);
		EasyJsonValue header;
		header[EASY_TAG_VERSION]	=	EASY_PROTOCOL_VERSION;
		header[EASY_TAG_CSEQ]		=	req.GetHeaderValue(EASY_TAG_CSEQ);

		switch(theErr)
		{
		case QTSS_BadArgument:
			header[EASY_TAG_ERROR_NUM]		= 400;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(400);
			break;
		case httpUnAuthorized:
			header[EASY_TAG_ERROR_NUM]		= 401;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(401);
			break;
		case QTSS_AttrNameExists:
			header[EASY_TAG_ERROR_NUM]		= 409;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(409);
			break;
		case EASY_ERROR_DEVICE_NOT_FOUND:
			header[EASY_TAG_ERROR_NUM]		= 603;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(603);
			break;
		case EASY_ERROR_SERVICE_NOT_FOUND:
			header[EASY_TAG_ERROR_NUM]		= 605;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(605);
			break;
		case httpRequestTimeout:
			header[EASY_TAG_ERROR_NUM]		= 408;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(408);
			break;
		case EASY_ERROR_SERVER_INTERNAL_ERROR:
			header[EASY_TAG_ERROR_NUM]		= 500;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(500);
			break;
		case EASY_ERROR_SERVER_NOT_IMPLEMENTED:
			header[EASY_TAG_ERROR_NUM]		= 501;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(501);
			break;
		default:
			header[EASY_TAG_ERROR_NUM]		= 400;
			header[EASY_TAG_ERROR_STRING]	= EasyDarwin::Protocol::EasyProtocol::GetErrorString(400);
			break;
		}

		rsp.SetHead(header);
		string msg = rsp.GetMsg();
		//������Ӧ����(HTTP Header)
		HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
		httpAck.CreateResponseHeader(!msg.empty()?httpOK:httpNotImplemented);
		if (!msg.empty())
			httpAck.AppendContentLengthHeader((UInt32)msg.length());

		char respHeader[2048] = { 0 };
		StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
		strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);

		HTTPResponseStream *pOutputStream = GetOutputStream();
		pOutputStream->Put(respHeader);

		//����Ӧ������ӵ�HTTP�����������
		if (!msg.empty()) 
			pOutputStream->Put((char*)msg.data(), msg.length());
	}
	return QTSS_NoErr;
}
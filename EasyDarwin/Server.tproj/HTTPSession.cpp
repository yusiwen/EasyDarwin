/*
	Copyleft (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
	File:       HTTPSession.cpp
	Contains:
*/

#include "HTTPSession.h"
#include "QTSServerInterface.h"
#include "OSArrayObjectDeleter.h"
#include "QTSSMemoryDeleter.h"
#include "QueryParamList.h"

#include "EasyProtocolDef.h"
#include "EasyProtocol.h"
#include "EasyUtil.h"
#include "Format.h"

#include "SocketUtils.h"

using namespace EasyDarwin::Protocol;
using namespace std;

#if __FreeBSD__ || __hpux__	
#include <unistd.h>
#endif

#include <errno.h>

#if __solaris__ || __linux__ || __sgi__	|| __hpux__
#include <crypt.h>
#endif

HTTPSession::HTTPSession()
	: HTTPSessionInterface(),
	fRequest(NULL),
	fReadMutex(),
	fCurrentModule(0),
	fState(kReadingFirstRequest)
{
	this->SetTaskName("HTTPSession");

	QTSServerInterface::GetServer()->AlterCurrentRTSPHTTPSessionCount(1);

	// Setup the QTSS param block, as none of these fields will change through the course of this session.
	fRoleParams.rtspRequestParams.inRTSPSession = this;
	fRoleParams.rtspRequestParams.inRTSPRequest = NULL;
	fRoleParams.rtspRequestParams.inClientSession = NULL;

	fModuleState.curModule = NULL;
	fModuleState.curTask = this;
	fModuleState.curRole = 0;
	fModuleState.globalLockRequested = false;
}

HTTPSession::~HTTPSession()
{
	char remoteAddress[20] = { 0 };
	StrPtrLen theIPAddressStr(remoteAddress, sizeof(remoteAddress));
	QTSS_GetValue(this, easyHTTPSesRemoteAddrStr, 0, static_cast<void*>(theIPAddressStr.Ptr), &theIPAddressStr.Len);

	char msgStr[2048] = { 0 };
	qtss_snprintf(msgStr, sizeof(msgStr), "HTTPSession offline from ip[%s]", remoteAddress);
	QTSServerInterface::LogError(qtssMessageVerbosity, msgStr);
	QTSServerInterface::GetServer()->AlterCurrentRTSPHTTPSessionCount(-1);

	// Invoke the session closing modules
	QTSS_RoleParams theParams;
	theParams.rtspSessionClosingParams.inRTSPSession = this;

	for (UInt32 x = 0; x < QTSServerInterface::GetNumModulesInRole(QTSSModule::kRTSPSessionClosingRole); x++)
		(void)QTSServerInterface::GetModule(QTSSModule::kRTSPSessionClosingRole, x)->CallDispatch(QTSS_RTSPSessionClosing_Role, &theParams);

	fLiveSession = false; //used in Clean up request to remove the RTP session.
	this->CleanupRequest();// Make sure that all our objects are deleted

}

SInt64 HTTPSession::Run()
{
	EventFlags events = this->GetEvents();
	QTSS_Error err = QTSS_NoErr;
	// Some callbacks look for this struct in the thread object
	OSThreadDataSetter theSetter(&fModuleState, NULL);

	if (events & Task::kKillEvent)
		fLiveSession = false;

	if (events & Task::kTimeoutEvent)
	{
		// Session��ʱ,�ͷ�Session 
		return -1;
	}

	while (this->IsLiveSession())
	{
		switch (fState)
		{
		case kReadingFirstRequest:
			{
				if ((err = fInputStream.ReadRequest()) == QTSS_NoErr)
				{
					//���RequestStream����QTSS_NoErr���ͱ�ʾ�Ѿ���ȡ��Ŀǰ���������������
					//���������ܹ���һ�����屨�ģ���Ҫ�����ȴ���ȡ...
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
		case kReadingRequest://��ȡ������
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
		case kHaveCompleteMessage://��ȡ��������������
			{
				Assert(fInputStream.GetRequestBuffer());

				Assert(fRequest == NULL);
				fRequest = new HTTPRequest(&QTSServerInterface::GetServerHeader(), fInputStream.GetRequestBuffer());

				//����������Ѿ���ȡ��һ��������Request����׼����������Ĵ���ֱ����Ӧ���ķ���
				//�ڴ˹����У���Session��Socket�������κ��������ݵĶ�/д��
				fReadMutex.Lock();
				fSessionMutex.Lock();

				fOutputStream.ResetBytesWritten();

				if (err == E2BIG)
				{
					//����HTTP���ģ�������408
					//(void)QTSSModuleUtils::SendErrorResponse(fRequest, qtssClientBadRequest, qtssMsgRequestTooLong);
					fState = kSendingResponse;
					break;
				}
				// Check for a corrupt base64 error, return an error
				if (err == QTSS_BadArgument)
				{
					//����HTTP���ģ�������408
					//(void)QTSSModuleUtils::SendErrorResponse(fRequest, qtssClientBadRequest, qtssMsgBadBase64);
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
				if (theErr == QTSS_WouldBlock)
				{
					this->ForceSameThread();
					fInputSocketP->RequestEvent(EV_RE);
					// We are holding mutexes, so we need to force
					// the same thread to be used for next Run()
					return 0;//����0��ʾ���¼��Ž���֪ͨ������>0��ʾ�涨�¼������Run

				}

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
				//����Ԥ�������
				//TODO:���Ĵ������
				fState = kCleaningUp;
				break;
			}

		case kProcessingRequest:
			{
				if (fOutputStream.GetBytesWritten() == 0)
				{
					// ��Ӧ���Ļ�û���γ�
					//QTSSModuleUtils::SendErrorResponse(fRequest, qtssServerInternal, qtssMsgNoModuleForRequest);
					fState = kCleaningUp;
					break;
				}

				fState = kSendingResponse;
			}
		case kSendingResponse:
			{
				//��Ӧ���ķ��ͣ�ȷ����ȫ����
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
					err = this->dumpRequestData();

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
		default: break;
		}
	}

	//���Sessionռ�õ�������Դ
	this->CleanupRequest();

	//Session������Ϊ0������-1��ϵͳ�Ὣ��Sessionɾ��
	if (fObjectHolders == 0)
		return -1;

	//��������ߵ����Sessionʵ���Ѿ���Ч�ˣ�Ӧ�ñ�ɾ������û�У���Ϊ���������ط�������Session����
	return 0;
}

QTSS_Error HTTPSession::SendHTTPPacket(StrPtrLen* contentXML, bool connectionClose, bool decrement)
{
    OSMutexLocker lock(&fSendMutex);

	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
	httpAck.CreateResponseHeader(contentXML->Len ? httpOK : httpNotImplemented);
	if (contentXML->Len)
		httpAck.AppendContentLengthHeader(contentXML->Len);

	if (connectionClose)
		httpAck.AppendConnectionCloseHeader();

    StrPtrLen type("application/json");
    httpAck.AppendResponseHeader(httpContentTypeHeader, &type);

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
	strncpy(respHeader, ackPtr->Ptr, ackPtr->Len);

	RTSPResponseStream *pOutputStream = GetOutputStream();
	pOutputStream->Put(respHeader);
	if (contentXML->Len > 0)
		pOutputStream->Put(contentXML->Ptr, contentXML->Len);

	if (pOutputStream->GetBytesWritten() != 0)
	{
		pOutputStream->Flush();
	}

	if (connectionClose)
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

		if (!sRequest.empty())
		{
			boost::to_lower(sRequest);

			vector<string> path;
			if (boost::ends_with(sRequest, "/"))
			{
				boost::erase_tail(sRequest, 1);
			}
			boost::split(path, sRequest, boost::is_any_of("/"), boost::token_compress_on);
			if (path.size() == 3)
			{

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "login")
				{
					return execNetMsgCSLoginReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "logout")
				{
					return execNetMsgCSLogoutReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getserverinfo")
				{
					return execNetMsgCSGetServerVersionReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getbaseconfig")
				{
					return execNetMsgCSGetBaseConfigReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "setbaseconfig")
				{
					return execNetMsgCSSetBaseConfigReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "restart")
				{
					return execNetMsgCSRestartServiceRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getdevicestream")
				{
					return execNetMsgCSGetDeviceStreamReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "livedevicestream")
				{
					return execNetMsgCSLiveDeviceStreamReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getrtsplivesessions")
				{
					return execNetMsgCSGetRTSPLiveSessionsRESTful(fRequest->GetQueryString());
				}
			}

			execNetMsgCSUsageAck();

			return QTSS_NoErr;
		}
	}

	//��ȡ����Content json���ݲ���

	//1����ȡjson���ֳ���
	StrPtrLen* lengthPtr = fRequest->GetHeaderValue(httpContentLengthHeader);
	StringParser theContentLenParser(lengthPtr);
	theContentLenParser.ConsumeWhitespace();
	UInt32 content_length = theContentLenParser.ConsumeInteger(NULL);

	if (content_length)
	{
		qtss_printf("HTTPSession read content-length:%d \n", content_length);
		// Check for the existence of 2 attributes in the request: a pointer to our buffer for
		// the request body, and the current offset in that buffer. If these attributes exist,
		// then we've already been here for this request. If they don't exist, add them.
		UInt32 theBufferOffset = 0;
		char* theRequestBody = NULL;
		UInt32 theLen = sizeof(theRequestBody);
		theErr = QTSS_GetValue(this, easyHTTPSesContentBody, 0, &theRequestBody, &theLen);

		if (theErr != QTSS_NoErr)
		{
			// First time we've been here for this request. Create a buffer for the content body and
			// shove it in the request.
			theRequestBody = new char[content_length + 1];
			memset(theRequestBody, 0, content_length + 1);
			theLen = sizeof(theRequestBody);
			theErr = QTSS_SetValue(this, easyHTTPSesContentBody, 0, &theRequestBody, theLen);// SetValue creates an internal copy.
			Assert(theErr == QTSS_NoErr);

			// Also store the offset in the buffer
			theLen = sizeof(theBufferOffset);
			theErr = QTSS_SetValue(this, easyHTTPSesContentBodyOffset, 0, &theBufferOffset, theLen);
			Assert(theErr == QTSS_NoErr);
		}

		theLen = sizeof(theBufferOffset);
		QTSS_GetValue(this, easyHTTPSesContentBodyOffset, 0, &theBufferOffset, &theLen);

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

		qtss_printf("Add Len:%d \n", theLen);
		if ((theErr == QTSS_WouldBlock) || (theLen < (content_length - theBufferOffset)))
		{
			//
			// Update our offset in the buffer
			theBufferOffset += theLen;
			(void)QTSS_SetValue(this, easyHTTPSesContentBodyOffset, 0, &theBufferOffset, sizeof(theBufferOffset));
			// The entire content body hasn't arrived yet. Request a read event and wait for it.

			Assert(theErr == QTSS_NoErr);
			return QTSS_WouldBlock;
		}

		Assert(theErr == QTSS_NoErr);

		OSCharArrayDeleter charArrayPathDeleter(theRequestBody);

		////���Ĵ������������
		//EasyDarwin::Protocol::EasyProtocol protocol(theRequestBody);
		//int nNetMsg = protocol.GetMessageType();

		//switch (nNetMsg)
		//{
		//	case MSG_DEV_CMS_REGISTER_REQ:
		//		ExecNetMsgDevRegisterReq(theRequestBody);
		//		break;
		//	case MSG_NGX_CMS_NEED_STREAM_REQ:
		//		ExecNetMsgNgxStreamReq(theRequestBody);
		//		break;
		//	default:
		//		ExecNetMsgDefaultReqHandler(theRequestBody);
		//		break;
		//}

		UInt32 offset = 0;
		(void)QTSS_SetValue(this, easyHTTPSesContentBodyOffset, 0, &offset, sizeof(offset));
		char* content = NULL;
		(void)QTSS_SetValue(this, easyHTTPSesContentBody, 0, &content, 0);

	}

	qtss_printf("get complete http msg:%s QueryString:%s \n", fRequest->GetRequestPath(), fRequest->GetQueryString());

	return QTSS_NoErr;
}

void HTTPSession::CleanupRequest()
{
	if (fRequest != NULL)
	{
		// NULL out any references to the current request
		delete fRequest;
		fRequest = NULL;
		fRoleParams.rtspRequestParams.inRTSPRequest = NULL;
		fRoleParams.rtspRequestParams.inRTSPHeaders = NULL;
	}

	fSessionMutex.Unlock();
	fReadMutex.Unlock();

	// Clear out our last value for request body length before moving onto the next request
	this->SetRequestBodyLength(-1);
}

bool HTTPSession::OverMaxConnections(UInt32 buffer)
{
	QTSServerInterface* theServer = QTSServerInterface::GetServer();
	SInt32 maxConns = theServer->GetPrefs()->GetMaxConnections();
	bool overLimit = false;

	if (maxConns > -1) // limit connections
	{
		UInt32 maxConnections = static_cast<UInt32>(maxConns) + buffer;
		if (theServer->GetNumRTSPSessions() > maxConnections)
		{
			overLimit = true;
		}
	}
	return overLimit;
}

QTSS_Error HTTPSession::dumpRequestData()
{
	char theDumpBuffer[QTSS_MAX_REQUEST_BUFFER_SIZE];

	QTSS_Error theErr = QTSS_NoErr;
	while (theErr == QTSS_NoErr)
		theErr = this->Read(theDumpBuffer, QTSS_MAX_REQUEST_BUFFER_SIZE, NULL);

	return theErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetRTSPLiveSessionsRESTful(const char* queryString)
{	
	QTSS_Error theErr = QTSS_NoErr;

	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
		StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	do
	{
		// ��ȡ��ӦContent
		char* msgContent = static_cast<char*>(Easy_GetRTSPPushSessions());

		StrPtrLen msgJson(msgContent);

		// ������Ӧ����(HTTPͷ)
		HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
		httpAck.CreateResponseHeader(msgJson.Len ? httpOK : httpNotImplemented);
		if (msgJson.Len)
			httpAck.AppendContentLengthHeader(msgJson.Len);

		httpAck.AppendConnectionCloseHeader();

		char respHeader[2048] = { 0 };
		StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
		strncpy(respHeader, ackPtr->Ptr, ackPtr->Len);

		// HTTP��ӦContent
		RTSPResponseStream *pOutputStream = GetOutputStream();
		pOutputStream->Put(respHeader);
		if (msgJson.Len > 0)
			pOutputStream->Put(msgJson.Ptr, msgJson.Len);

		delete[] msgContent;
	} while (false);

	return theErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetRTSPRecordSessionsRESTful(const char* queryString)
{
	QTSS_Error theErr = QTSS_NoErr;

	string queryTemp;
	if (queryString != NULL)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}

	QueryParamList parList(const_cast<char *>(queryTemp.c_str()));
	const char* startTime = parList.DoFindCGIValueForParam(EASY_TAG_L_START_TIME);//username
	const char* name = parList.DoFindCGIValueForParam(EASY_TAG_L_NAME);//username
	const char* endTime = parList.DoFindCGIValueForParam(EASY_TAG_L_END_TIME);//username

	do
	{
		char* msgContent = NULL;

		StrPtrLen msgJson(msgContent);

		HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);
		httpAck.CreateResponseHeader(msgJson.Len ? httpOK : httpNotImplemented);
		if (msgJson.Len)
			httpAck.AppendContentLengthHeader(msgJson.Len);

		httpAck.AppendConnectionCloseHeader();

		char respHeader[2048] = { 0 };
		StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
		strncpy(respHeader, ackPtr->Ptr, ackPtr->Len);

		RTSPResponseStream *pOutputStream = GetOutputStream();
		pOutputStream->Put(respHeader);
		if (msgJson.Len > 0)
			pOutputStream->Put(msgJson.Ptr, msgJson.Len);

		delete[] msgContent;
	} while (false);

	return theErr;
}

QTSS_Error HTTPSession::execNetMsgCSUsageAck()
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	EasyProtocolACK rsp(MSG_SC_SERVER_USAGES_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	Json::Value* proot = rsp.GetRoot();

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "GetServerInfo";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/getserverinfo";
		value[EASY_TAG_DESCRIPTION] = "get server information";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "Login";
		value[EASY_TAG_PARAMETER] = "username=[UserName]&password=[Password]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/login?username=admin&password=admin";
		value[EASY_TAG_DESCRIPTION] = "user login";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "Logout";
		value[EASY_TAG_PARAMETER] = "http Cookie Header with Token";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/logout";
		value[EASY_TAG_DESCRIPTION] = "user logout";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "GetBaseConfig";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/getbaseconfig";
		value[EASY_TAG_DESCRIPTION] = "get server base config";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "SetBaseConfig";
		value[EASY_TAG_PARAMETER] = "RTSPLanPort=554&RTSPWanPort=10554&ServiceLanPort=10008&ServiceWanIP=192.168.66.121&ServiceWanPort=10008";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/setbaseconfig?RTSPWanPort=8554&ServiceWanIP=8.8.8.8...";
		value[EASY_TAG_DESCRIPTION] = "set server base config";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "GetRTSPLiveSessions";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/getrtsplivesessions";
		value[EASY_TAG_DESCRIPTION] = "get live sessions";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "GetDeviceStream";
		value[EASY_TAG_PARAMETER] = "device=[Serial]&channel=[Channel]&protocol=[RTSP]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/getdevicestream?device=00001&channel=1&protocol=RTSP";
		value[EASY_TAG_DESCRIPTION] = "get device channel stream";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "Restart";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/[Version]/restart";
		value[EASY_TAG_DESCRIPTION] = "restart EasyDarwin";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	rsp.SetHead(header);
	rsp.SetBody(body);
	string msg = rsp.GetMsg();
	StrPtrLen theValueAck(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValueAck, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetServerVersionReqRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
		StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	EasyProtocolACK rsp(MSG_SC_SERVER_INFO_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	char* serverHeader = NULL;
	(void)QTSS_GetValueAsString(QTSServerInterface::GetServer(), qtssSvrRTSPServerHeader, 0, &serverHeader);
	QTSSCharArrayDeleter theFullPathStrDeleter(serverHeader);
	body[EASY_TAG_SERVER_HEADER] = serverHeader;

	SInt64 timeNow = OS::Milliseconds();
	SInt64 startupTime = 0;
	UInt32 startupTimeSize = sizeof(startupTime);
	(void)QTSS_GetValue(QTSServerInterface::GetServer(), qtssSvrStartupTime, 0, &startupTime, &startupTimeSize);
	SInt64 longstTime = (timeNow - startupTime) / 1000;

	unsigned int timeDays = longstTime / (24 * 60 * 60);
	unsigned int timeHours = (longstTime % (24 * 60 * 60)) / (60 * 60);
	unsigned int timeMins = ((longstTime % (24 * 60 * 60)) % (60 * 60)) / 60;
	unsigned int timeSecs = ((longstTime % (24 * 60 * 60)) % (60 * 60)) % 60;

	body[EASY_TAG_SERVER_RUNNING_TIME] = Format("%u Days %u Hours %u Mins %u Secs", timeDays, timeHours, timeMins, timeSecs);

	body[EASY_TAG_SERVER_HARDWARE] = "x86";
	body[EASY_TAG_SERVER_INTERFACE_VERSION] = "v1";

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, true, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSLoginReqRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
		StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	string queryTemp;
	if (queryString != NULL)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}

	QueryParamList parList(const_cast<char *>(queryTemp.c_str()));
	const char* chUserName = parList.DoFindCGIValueForParam(EASY_TAG_L_USER_NAME);//username
	const char* chPassword = parList.DoFindCGIValueForParam(EASY_TAG_L_PASSWORD);//password

	QTSS_Error theErr = QTSS_BadArgument;

	if (!chUserName || !chPassword)
	{
		return theErr;
	}

	/*
	*TODO: You need do your own self Login check!!!
	*Here is only a demo. user: admin  password : admin
	*passward encoded with MD5
	*/
	if (strcmp(chUserName, "admin") != 0 || strcmp(chPassword, "21232f297a57a5a743894a0e4a801fc3") != 0)
	{
		return theErr;
	}

	//Authentication->token->redis->Platform use
	theErr = QTSS_NoErr;

	EasyProtocolACK rsp(MSG_SC_SERVER_LOGIN_ACK);
	EasyJsonValue header, body;

	body[EASY_TAG_TOKEN] = "EasyDarwinTempToken";

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	int errNo = theErr == QTSS_NoErr ? EASY_ERROR_SUCCESS_OK : EASY_ERROR_CLIENT_UNAUTHORIZED;
	header[EASY_TAG_ERROR_NUM] = errNo;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(errNo);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSLogoutReqRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
		StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}
	//cookie->token->clear redis->Platform clear,need login again

	EasyProtocolACK rsp(MSG_SC_SERVER_LOGOUT_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetBaseConfigReqRESTful(const char* queryString)
{
    QTSS_RoleParams theParams;
    theParams.easyStreamInfoParams.inStreamName = "alex";
    theParams.easyStreamInfoParams.inChannel = 1;
    theParams.easyStreamInfoParams.inNumOutputs = 1;
    theParams.easyStreamInfoParams.inAction = easyRedisActionSet;
    UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kRedisUpdateStreamInfoRole);
    for (UInt32 currentModule = 0; currentModule < numModules; currentModule++)
    {
        QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kRedisUpdateStreamInfoRole, currentModule);
        (void)theModule->CallDispatch(Easy_RedisUpdateStreamInfo_Role, &theParams);
    }

    QTSS_RoleParams theParams2;
    theParams2.easyStreamInfoParams.inStreamName = "alex";
    theParams2.easyStreamInfoParams.inChannel = 1;
    UInt32 numModules2 = QTSServerInterface::GetNumModulesInRole(QTSSModule::kEasyCMSFreeStreamRole);
    for (UInt32 currentModule = 0; currentModule < numModules2; currentModule++)
    {
        QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kEasyCMSFreeStreamRole, currentModule);
        (void)theModule->CallDispatch(Easy_CMSFreeStream_Role, &theParams2);
    }

	if(QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
		StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	EasyProtocolACK rsp(MSG_SC_SERVER_BASE_CONFIG_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	UInt16 port;
	UInt32 len = sizeof(UInt16);
	(void)QTSS_GetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsRTSPPorts, 0, static_cast<void*>(&port), &len);
	body[EASY_TAG_CONFIG_RTSP_LAN_PORT] = EasyUtil::ToString(port);

	char lanip[512] = { 0 };
	for (UInt32 ipAddrIter = 0; ipAddrIter < SocketUtils::GetNumIPAddrs(); ipAddrIter++)
	{
		StrPtrLen* ipIter = SocketUtils::GetIPAddrStr(ipAddrIter);
		::strncat(lanip, ipIter->Ptr, ipIter->Len);
		strcat(lanip,"; ");
	}
	body[EASY_TAG_CONFIG_SERVICE_LAN_IP] = lanip;

	body[EASY_TAG_CONFIG_RTSP_WAN_PORT] = EasyUtil::ToString(QTSServerInterface::GetServer()->GetPrefs()->GetRTSPWANPort());

	body[EASY_TAG_CONFIG_SERVICE_WAN_IP] = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP();

	body[EASY_TAG_CONFIG_SERVICE_LAN_PORT] = EasyUtil::ToString(QTSServerInterface::GetServer()->GetPrefs()->GetServiceLanPort());
	body[EASY_TAG_CONFIG_SERVICE_WAN_PORT] = EasyUtil::ToString(QTSServerInterface::GetServer()->GetPrefs()->GetServiceWanPort());

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSSetBaseConfigReqRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
        StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	string queryTemp;
	if (queryString)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}
	QueryParamList parList(const_cast<char *>(queryTemp.c_str()));

	//1.EASY_TAG_CONFIG_RTSP_LAN_PORT
	const char* chRTSPLanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_RTSP_LAN_PORT);
	if (chRTSPLanPort)
	{
		UInt16 uRTSPLanPort = atoi(chRTSPLanPort);
		(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsRTSPPorts, 0, &uRTSPLanPort, sizeof(uRTSPLanPort));
	}

	//2.EASY_TAG_CONFIG_RTSP_WAN_PORT
	const char*	chRTSPWanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_RTSP_WAN_PORT);
	if (chRTSPWanPort)
	{
		UInt16 uRTSPWanPort = atoi(chRTSPWanPort);
		(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), easyPrefsRTSPWANPort, 0, &uRTSPWanPort, sizeof(uRTSPWanPort));
	}

	//3.EASY_TAG_CONFIG_SERVICE_LAN_IP
	//const char* chLanIP = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_LAN_IP);
	//if (chLanIP)
	//	(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsRTSPIPAddr, 0, (void*)chLanIP, strlen(chLanIP));


	//4.EASY_TAG_CONFIG_SERVICE_WAN_IP
	const char* chWanIP = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_WAN_IP);
	if (chWanIP)
		(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), easyPrefsServiceWANIPAddr, 0, (void*)chWanIP, strlen(chWanIP));

	//5.EASY_TAG_CONFIG_SERVICE_LAN_PORT
	const char* chHTTPLanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_LAN_PORT);
	if (chHTTPLanPort)
	{
		UInt16 uHTTPLanPort = atoi(chHTTPLanPort);
		(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), easyPrefsHTTPServiceLanPort, 0, &uHTTPLanPort, sizeof(uHTTPLanPort));
	}

	//6.EASY_TAG_CONFIG_SERVICE_WAN_PORT
	const char*	chHTTPWanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_WAN_PORT);
	if (chHTTPWanPort)
	{
		UInt16 uHTTPWanPort = atoi(chHTTPWanPort);
		(void)QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), easyPrefsHTTPServiceWanPort, 0, &uHTTPWanPort, sizeof(uHTTPWanPort));
	}

	EasyProtocolACK rsp(MSG_SC_SERVER_SET_BASE_CONFIG_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSRestartServiceRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
        StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

#ifdef WIN32
	::ExitProcess(0);
#else
	exit(0);
#endif //__WIN32__
}

QTSS_Error HTTPSession::execNetMsgCSGetDeviceStreamReqRESTful(const char* queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
        StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	string queryTemp;
	if (queryString)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}
	QueryParamList parList(const_cast<char *>(queryTemp.c_str()));

	const char* chSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);
	const char* chProtocol = parList.DoFindCGIValueForParam(EASY_TAG_L_PROTOCOL);
	//const char* chReserve = parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);

	UInt32 theChannelNum = 1;
	EasyStreamType streamType = easyIllegalStreamType;

	char* outURL = new char[QTSS_MAX_URL_LENGTH];
	outURL[0] = '\0';
	QTSSCharArrayDeleter theHLSURLDeleter(outURL);

	bool outIsReady = false;

	int theErr = EASY_ERROR_SERVER_NOT_IMPLEMENTED;

	do
	{
		if (!chSerial)
		{
			theErr = EASY_ERROR_NOT_FOUND;
			break;
		}

		const char* chChannel = parList.DoFindCGIValueForParam(EASY_TAG_CHANNEL);
		if (!chChannel || string(chChannel).empty())
		{
			theChannelNum = 1;
		}
		else
		{
			try
			{
				theChannelNum = atoi(chChannel);
			}
			catch(...)
			{
				theChannelNum = 1;
			}
		}

		if (!chProtocol)
		{
			theErr = EASY_ERROR_CLIENT_BAD_REQUEST;
			break;
		}

		QTSS_RoleParams params;
		params.easyGetDeviceStreamParams.inDevice = const_cast<char*>(chSerial);
		params.easyGetDeviceStreamParams.inChannel = theChannelNum;
		params.easyGetDeviceStreamParams.inStreamType = easyRTSPType;
		params.easyGetDeviceStreamParams.outUrl = outURL;
		params.easyGetDeviceStreamParams.outIsReady = false;

		UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kGetDeviceStreamRole);
		for (UInt32 fCurrentModule = 0; fCurrentModule < numModules; fCurrentModule++)
		{
			QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kGetDeviceStreamRole, fCurrentModule);
			QTSS_Error exeErr = theModule->CallDispatch(Easy_GetDeviceStream_Role, &params);
			if (exeErr == QTSS_NoErr)
			{
				theErr = EASY_ERROR_SUCCESS_OK;
				outIsReady = params.easyGetDeviceStreamParams.outIsReady;
				break;
			}
		}
	} while (false);


	EasyProtocolACK rsp(MSG_SC_GET_STREAM_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = theErr;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(theErr);

	if (theErr == EASY_ERROR_SUCCESS_OK)
	{
		body[EASY_TAG_URL] = outURL;
		body[EASY_TAG_PROTOCOL] = "RTSP";
		body[EASY_TAG_IS_READY] = outIsReady;
	}

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSLiveDeviceStreamReqRESTful(const char * queryString)
{
	if (QTSServerInterface::GetServer()->GetPrefs()->CloudPlatformEnabled())
	{
		//printf("if cloud platform enabled,we will check platform login token");
        StrPtrLen* cokieTemp = fRequest->GetHeaderValue(httpCookieHeader);
		//if (!hasLogin(cokieTemp))
		//{
		//	return EASY_ERROR_CLIENT_UNAUTHORIZED;
		//}
	}

	string queryTemp;
	if (queryString)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}
	QueryParamList parList(const_cast<char *>(queryTemp.c_str()));

	const char* chSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);
	const char* chProtocol = parList.DoFindCGIValueForParam(EASY_TAG_L_PROTOCOL);
	//const char* chReserve = parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);

	UInt32 theChannelNum = 1;
	EasyStreamType streamType = easyIllegalStreamType;

	int theErr = EASY_ERROR_SERVER_NOT_IMPLEMENTED;

	do
	{
		if (!chSerial)
		{
			theErr = EASY_ERROR_NOT_FOUND;
			break;
		}

		const char* chChannel = parList.DoFindCGIValueForParam(EASY_TAG_CHANNEL);
		if (chChannel)
		{
			theChannelNum = atoi(chChannel);
		}

		if (!chProtocol)
		{
			theErr = EASY_ERROR_CLIENT_BAD_REQUEST;
			break;
		}

		StrPtrLen chProtocolPtr(const_cast<char*>(chProtocol));
		streamType = HTTPProtocol::GetStreamType(&chProtocolPtr);

		if (streamType == easyIllegalStreamType)
		{
			theErr = EASY_ERROR_CLIENT_BAD_REQUEST;
			break;
		}

		QTSS_RoleParams params;
		params.easyGetDeviceStreamParams.inDevice = const_cast<char*>(chSerial);
		params.easyGetDeviceStreamParams.inChannel = theChannelNum;
		params.easyGetDeviceStreamParams.inStreamType = streamType;
		params.easyGetDeviceStreamParams.outUrl = NULL;
		params.easyGetDeviceStreamParams.outIsReady = false;

		UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kLiveDeviceStreamRole);
		for (UInt32 fCurrentModule = 0; fCurrentModule < numModules; fCurrentModule++)
		{
			QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kLiveDeviceStreamRole, fCurrentModule);
			QTSS_Error exeErr = theModule->CallDispatch(Easy_LiveDeviceStream_Role, &params);
			if (exeErr == QTSS_NoErr)
			{
				theErr = EASY_ERROR_SUCCESS_OK;
				break;
			}
		}
	} while (false);

	EasyProtocolACK rsp(MSG_SC_GET_STREAM_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = theErr;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(theErr);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	StrPtrLen theValue(const_cast<char*>(msg.c_str()), msg.size());
	this->SendHTTPPacket(&theValue, false, false);

	return QTSS_NoErr;
}
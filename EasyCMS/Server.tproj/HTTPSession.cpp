/*
	Copyleft (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
	File:       HTTPSession.cpp
	Contains:   EasyCMS HTTPSession
*/

#include "HTTPSession.h"

#include "QTSServerInterface.h"
#include "OSArrayObjectDeleter.h"
#include "EasyUtil.h"
#include "QueryParamList.h"
#include "Format.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <unordered_set>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


#if __FreeBSD__ || __hpux__	
#include <unistd.h>
#endif

#include <errno.h>

#if __solaris__ || __linux__ || __sgi__	|| __hpux__
#include <crypt.h>
#endif

using namespace std;

static const int sWaitDeviceRspTimeout = 10;
static const int sHeaderSize = 2048;
static const int sIPSize = 20;
static const int sPortSize = 6;

#define	WIDTHBYTES(c)		((c+31)/32*4)	// c = width * bpp
#define	SNAP_CAPTURE_TIME	30
#define SNAP_IMAGE_WIDTH	320
#define	SNAP_IMAGE_HEIGHT	180
#define	SNAP_SIZE			SNAP_IMAGE_WIDTH * SNAP_IMAGE_HEIGHT * 3 + 58

HTTPSession::HTTPSession()
	: HTTPSessionInterface()
	, fRequest(nullptr)
	, fState(kReadingFirstRequest)
{
	this->SetTaskName("HTTPSession");

	//All EasyCameraSession/EasyNVRSession/EasyHTTPSession
	QTSServerInterface::GetServer()->AlterCurrentHTTPSessionCount(1);

	fModuleState.curModule = nullptr;
	fModuleState.curTask = this;
	fModuleState.curRole = 0;
	fModuleState.globalLockRequested = false;

	auto sessionMap = QTSServerInterface::GetServer()->GetHTTPSessionMap();
	sessionMap->Register(sessionId_, this);

	qtss_printf("Create HTTPSession:%s\n", sessionId_.c_str());
}

HTTPSession::~HTTPSession()
{
	if (GetSessionType() == EasyHTTPSession)
	{
		OSMutex* mutexMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMutex();
		OSHashMap* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMap();
		OSRefIt itRef;
		{
			OSMutexLocker lock(mutexMap);
			int iDevNum = 0;

			for (itRef = deviceMap->begin(); itRef != deviceMap->end(); ++itRef)
			{
				HTTPSession* session = static_cast<HTTPSession*>(itRef->second->GetObjectPtr());
				if (session->GetTalkbackSession() == sessionId_)
				{
					session->SetTalkbackSession("");
				}
			}
		}
	}



	fLiveSession = false;
	this->cleanupRequest();

	QTSServerInterface::GetServer()->AlterCurrentHTTPSessionCount(-1);

	auto sessionMap = QTSServerInterface::GetServer()->GetHTTPSessionMap();
	sessionMap->UnRegister(sessionId_);

	qtss_printf("Release HTTPSession:%s Serial:%s Type:%d\n", sessionId_.c_str(), device_->serial_.c_str(), device_->eAppType);

	if (fRequestBody)
	{
		delete[] fRequestBody;
		fRequestBody = nullptr;
	}
}

SInt64 HTTPSession::Run()
{
	EventFlags events = this->GetEvents();
	QTSS_Error err = QTSS_NoErr;

	// Some callbacks look for this struct in the thread object
	OSThreadDataSetter theSetter(&fModuleState, nullptr);

	if (events & kKillEvent)
		fLiveSession = false;

	if (events & kTimeoutEvent)
	{
		string msgStr = Format("Timeout HTTPSession��Device_serial[%s]\n", device_->serial_);
		QTSServerInterface::LogError(qtssMessageVerbosity, const_cast<char*>(msgStr.c_str()));
		fLiveSession = false;
		this->Signal(kKillEvent);
	}

	while (this->IsLiveSession())
	{
		switch (fState)
		{
		case kReadingFirstRequest:
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
			continue;

		case kReadingRequest:
		{
			OSMutexLocker readMutexLocker(&fReadMutex);

			if ((err = fInputStream.ReadRequest()) == QTSS_NoErr)
			{
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
				Assert(!this->IsLiveSession());
				break;
			}
			fState = kHaveCompleteMessage;
		}
		case kHaveCompleteMessage:

			Assert(fInputStream.GetRequestBuffer());

			Assert(fRequest == nullptr);
			fRequest = new HTTPRequest(&QTSServerInterface::GetServerHeader(), fInputStream.GetRequestBuffer());

			fReadMutex.Lock();
			fSessionMutex.Lock();

			fOutputStream.ResetBytesWritten();

			if ((err == E2BIG) || (err == QTSS_BadArgument))
			{
				execNetMsgErrorReqHandler(httpBadRequest);
				fState = kSendingResponse;
				break;
			}

			Assert(err == QTSS_RequestArrived);
			fState = kFilteringRequest;

		case kFilteringRequest:
		{
			fTimeoutTask.RefreshTimeout();

			if (fSessionType != EasyHTTPSession && !device_->serial_.empty())
			{
				addDevice();
			}

			QTSS_Error theErr = setupRequest();

			if (theErr == QTSS_WouldBlock)
			{
				this->ForceSameThread();
				fInputSocketP->RequestEvent(EV_RE);
				// We are holding mutexes, so we need to force
				// the same thread to be used for next Run()
				return 0;
			}

			if (theErr != QTSS_NoErr)
			{
				execNetMsgErrorReqHandler(httpBadRequest);
			}

			if (fOutputStream.GetBytesWritten() > 0)
			{
				fState = kSendingResponse;
				break;
			}

			fState = kPreprocessingRequest;
			break;
		}

		case kPreprocessingRequest:
			processRequest();

			if (fOutputStream.GetBytesWritten() > 0)
			{
				delete[] fRequestBody;
				fRequestBody = nullptr;
				fState = kSendingResponse;
				break;
			}

			delete[] fRequestBody;
			fRequestBody = nullptr;
			fState = kCleaningUp;
			break;

		case kProcessingRequest:
			if (fOutputStream.GetBytesWritten() == 0)
			{
				execNetMsgErrorReqHandler(httpInternalServerError);
				fState = kSendingResponse;
				break;
			}

			fState = kSendingResponse;
		case kSendingResponse:
			Assert(fRequest != nullptr);

			err = fOutputStream.Flush();

			if (err == EAGAIN)
			{
				// If we get this error, we are currently flow-controlled and should
				// wait for the socket to become writeable again
				fSocket.RequestEvent(EV_WR);
				this->ForceSameThread();
				// We are holding mutexes, so we need to force
				// the same thread to be used for next Run()
				return 0;
			}
			if (err != QTSS_NoErr)
			{
				// Any other error means that the client has disconnected, right?
				Assert(!this->IsLiveSession());
				break;
			}

			fState = kCleaningUp;

		case kCleaningUp:
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

			this->cleanupRequest();

			fState = kReadingRequest;
		default: break;
		}
	}

	this->cleanupRequest();

	if (fObjectHolders == 0)
		return -1;

	return 0;
}

QTSS_Error HTTPSession::SendHTTPPacket(const string& msg, bool connectionClose, bool decrement)
{
	OSMutexLocker lock(&fSendMutex);

	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);

	if (httpAck.CreateResponseHeader(httpOK))
	{
		if (!msg.empty())
			httpAck.AppendContentLengthHeader(static_cast<UInt32>(msg.size()));

		if (connectionClose)
			httpAck.AppendConnectionCloseHeader();

		StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
		string sendString(ackPtr->Ptr, ackPtr->Len);
		if (!msg.empty())
			sendString.append(msg.c_str(), msg.size());

		UInt32 theLengthSent = 0;
		UInt32 amtInBuffer = sendString.size();
		do
		{
			QTSS_Error theErr = fOutputSocketP->Send(sendString.c_str(), amtInBuffer, &theLengthSent);

			if (theErr != QTSS_NoErr && theErr != EAGAIN)
			{
				return theErr;
			}

			if (theLengthSent == amtInBuffer)
			{
				// We were able to send all the data in the buffer. Great. Flush it.
				return QTSS_NoErr;
			}
			// Not all the data was sent, so report an EAGAIN
			sendString.erase(0, theLengthSent);
			amtInBuffer = sendString.size();
			theLengthSent = 0;

		} while (amtInBuffer > 0);
	}

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::setupRequest()
{
	QTSS_Error theErr = fRequest->Parse();
	if (theErr != QTSS_NoErr)
		return QTSS_BadArgument;

	if (fRequest->GetRequestPath() != nullptr)
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
				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getdevicelist")
				{
					return execNetMsgCSGetDeviceListReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "getdeviceinfo")
				{
					return execNetMsgCSGetCameraListReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "startdevicestream")
				{
					return execNetMsgCSStartStreamReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "stopdevicestream")
				{
					return execNetMsgCSStopStreamReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "ptzcontrol")
				{
					return execNetMsgCSPTZControlReqRESTful(fRequest->GetQueryString());
				}

				if (path[0] == "api" && path[1] == EASY_PROTOCOL_VERSION && path[2] == "presetcontrol")
				{
					return execNetMsgCSPresetControlReqRESTful(fRequest->GetQueryString());
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
					return execNetMsgCSRestartReqRESTful(fRequest->GetQueryString());
				}
			}

			execNetMsgCSGetUsagesReqRESTful(nullptr);

			return QTSS_NoErr;
		}
	}

	//READ json Content

	//1��get json content length
	StrPtrLen* lengthPtr = fRequest->GetHeaderValue(httpContentLengthHeader);

	StringParser theContentLenParser(lengthPtr);
	theContentLenParser.ConsumeWhitespace();
	UInt32 content_length = theContentLenParser.ConsumeInteger(nullptr);

	//qtss_printf("HTTPSession read content-length:%d \n", content_length);

	if (content_length <= 0)
	{
		return QTSS_BadArgument;
	}

	// Check for the existence of 2 attributes in the request: a pointer to our buffer for
	// the request body, and the current offset in that buffer. If these attributes exist,
	// then we've already been here for this request. If they don't exist, add them.
	UInt32 theBufferOffset = 0;
	char* theRequestBody = nullptr;
	UInt32 theLen = sizeof(theRequestBody);
	theErr = QTSS_GetValue(this, EasyHTTPSesContentBody, 0, &theRequestBody, &theLen);

	if (theErr != QTSS_NoErr)
	{
		// First time we've been here for this request. Create a buffer for the content body and
		// shove it in the request.
		theRequestBody = new char[content_length + 1];
		memset(theRequestBody, 0, content_length + 1);
		theLen = sizeof(theRequestBody);
		theErr = QTSS_SetValue(this, EasyHTTPSesContentBody, 0, &theRequestBody, theLen);// SetValue creates an internal copy.
		Assert(theErr == QTSS_NoErr);

		// Also store the offset in the buffer
		theLen = sizeof(theBufferOffset);
		theErr = QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, theLen);
		Assert(theErr == QTSS_NoErr);
	}

	theLen = sizeof(theBufferOffset);
	QTSS_GetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, &theLen);

	// We have our buffer and offset. Read the data.
	//theErr = QTSS_Read(this, theRequestBody + theBufferOffset, content_length - theBufferOffset, &theLen);
	theErr = fInputStream.Read(theRequestBody + theBufferOffset, content_length - theBufferOffset, &theLen);
	Assert(theErr != QTSS_BadArgument);

	if ((theErr != QTSS_NoErr) && (theErr != EAGAIN))
	{
		OSCharArrayDeleter charArrayPathDeleter(theRequestBody);

		// NEED TO RETURN HTTP ERROR RESPONSE
		return QTSS_RequestFailed;
	}
	/*
	if (theErr == QTSS_RequestFailed)
	{
		OSCharArrayDeleter charArrayPathDeleter(theRequestBody);

		// NEED TO RETURN HTTP ERROR RESPONSE
		return QTSS_RequestFailed;
	}
	*/

	//qtss_printf("HTTPSession read content-length:%d (%d/%d) \n", theLen, theBufferOffset + theLen, content_length);
	if ((theErr == QTSS_WouldBlock) || (theLen < (content_length - theBufferOffset)))
	{
		//
		// Update our offset in the buffer
		theBufferOffset += theLen;
		(void)QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &theBufferOffset, sizeof(theBufferOffset));
		// The entire content body hasn't arrived yet. Request a read event and wait for it.

		Assert(theErr == QTSS_NoErr);
		return QTSS_WouldBlock;
	}

	// get complete HTTPHeader+JSONContent
	fRequestBody = theRequestBody;
	Assert(theErr == QTSS_NoErr);

	//if (theBufferOffset < sHeaderSize)
	//qtss_printf("Recv message: %s\n", fRequestBody);

	UInt32 offset = 0;
	(void)QTSS_SetValue(this, EasyHTTPSesContentBodyOffset, 0, &offset, sizeof(offset));
	char* content = nullptr;
	(void)QTSS_SetValue(this, EasyHTTPSesContentBody, 0, &content, 0);

	return theErr;
}

void HTTPSession::cleanupRequest()
{
	if (fRequest != nullptr)
	{
		// nullptr out any references to the current request
		delete fRequest;
		fRequest = nullptr;
	}

	fSessionMutex.Unlock();
	fReadMutex.Unlock();

	// Clear out our last value for request body length before moving onto the next request
	this->SetRequestBodyLength(-1);
}

bool HTTPSession::overMaxConnections(UInt32 buffer)
{
	QTSServerInterface* theServer = QTSServerInterface::GetServer();
	SInt32 maxConns = theServer->GetPrefs()->GetMaxConnections();
	bool overLimit = false;

	if (maxConns > -1) // limit connections
	{
		UInt32 maxConnections = static_cast<UInt32>(maxConns) + buffer;
		if (theServer->GetNumServiceSessions() > maxConnections)
		{
			overLimit = true;
		}
	}
	return overLimit;
}

QTSS_Error HTTPSession::dumpRequestData()
{
	char theDumpBuffer[EASY_REQUEST_BUFFER_SIZE_LEN];

	QTSS_Error theErr = QTSS_NoErr;
	while (theErr == QTSS_NoErr)
		theErr = this->Read(theDumpBuffer, EASY_REQUEST_BUFFER_SIZE_LEN, nullptr);

	return theErr;
}

QTSS_Error HTTPSession::execNetMsgDSPostSnapReq(const char* json)
{
	if (!fAuthenticated) return httpUnAuthorized;

	EasyMsgDSPostSnapREQ parse(json);

	string image = parse.GetBodyValue(EASY_TAG_IMAGE);
	string channel = parse.GetBodyValue(EASY_TAG_CHANNEL);
	string device_serial = parse.GetBodyValue(EASY_TAG_SERIAL);
	string strType = parse.GetBodyValue(EASY_TAG_TYPE);
	string strTime = parse.GetBodyValue(EASY_TAG_TIME);
	string reserve = parse.GetBodyValue(EASY_TAG_RESERVE);

	if (channel.empty())
		channel = "1";

	if (strTime.empty())
	{
		strTime = EasyUtil::NowTime(EASY_TIME_FORMAT_YYYYMMDDHHMMSSEx);
	}
	else//Time Filter 2015-07-20 12:55:30->20150720125530
	{
		EasyUtil::DelChar(strTime, '-');
		EasyUtil::DelChar(strTime, ':');
		EasyUtil::DelChar(strTime, ' ');
	}

	if (device_serial.empty() || image.empty() || strType.empty() || strTime.empty())
	{
		return QTSS_BadArgument;
	}

	image = EasyUtil::Base64Decode(image.data(), image.size());

	string jpgDir = string(QTSServerInterface::GetServer()->GetPrefs()->GetSnapLocalPath()).append(device_serial);
	OS::RecursiveMakeDir(const_cast<char*>(jpgDir.c_str()));
	string jpgPath = Format("%s/%s_%s_%s.%s", jpgDir, device_serial, channel, strTime, EasyProtocol::GetSnapTypeString(EASY_SNAP_TYPE_JPEG));

	auto picType = EasyProtocol::GetSnapType(strType);

	if (picType == EASY_SNAP_TYPE_JPEG)
	{
		FILE* fSnap = ::fopen(jpgPath.c_str(), "wb");
		if (!fSnap)
		{
			return QTSS_NoErr;
		}
		fwrite(image.data(), 1, image.size(), fSnap);
		fclose(fSnap);
	}

	//web path

	device_->channels_[channel].status_ = "online";

	string snapURL = Format("%s%s/%s_%s_%s.%s", string(QTSServerInterface::GetServer()->GetPrefs()->GetSnapWebPath()), device_serial,
		device_serial, channel, strTime, EasyProtocol::GetSnapTypeString(EASY_SNAP_TYPE_JPEG));
	device_->HoldSnapPath(snapURL, channel);

	EasyProtocolACK rsp(MSG_SD_POST_SNAP_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = parse.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_SERIAL] = device_serial;
	body[EASY_TAG_CHANNEL] = channel;

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgErrorReqHandler(HTTPStatusCode errCode)
{
	//HTTP Header
	HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);

	if (httpAck.CreateResponseHeader(errCode))
	{
		StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
		UInt32 theLengthSent = 0;
		QTSS_Error theErr = fOutputSocketP->Send(ackPtr->Ptr, ackPtr->Len, &theLengthSent);
		if (theErr != QTSS_NoErr && theErr != EAGAIN)
		{
			return theErr;
		}
	}

	this->fLiveSession = false;

	return QTSS_NoErr;
}

void HTTPSession::addDevice() const
{
	QTSS_RoleParams theParams;
	theParams.DeviceInfoParams.serial_ = new char[64];
	theParams.DeviceInfoParams.channels_ = new char[64];
	theParams.DeviceInfoParams.deviceType_ = new char[64];
	theParams.DeviceInfoParams.type_ = new char[64];
	theParams.DeviceInfoParams.token_ = new char[64];
	strncpy(theParams.DeviceInfoParams.serial_, device_->serial_.c_str(), device_->serial_.size() + 1);
	strncpy(theParams.DeviceInfoParams.token_, device_->password_.c_str(), device_->password_.size() + 1);

	string type = EasyProtocol::GetAppTypeString(device_->eAppType);
	string channel;
	if (device_->eAppType == EASY_APP_TYPE_CAMERA)
	{
		channel = "1";
	}
	else if (device_->eAppType == EASY_APP_TYPE_NVR)
	{
		for (EasyDevices::iterator it = device_->channels_.begin(); it != device_->channels_.end(); ++it)
		{
			channel.append("/");
			channel += it->first;
		}

	}

	if (channel.empty())
	{
		channel = "0";
	}

	auto deviceType = EasyProtocol::GetTerminalTypeString(device_->eDeviceType);

	strncpy(theParams.DeviceInfoParams.channels_, channel.c_str(), channel.size() + 1);
	strncpy(theParams.DeviceInfoParams.deviceType_, deviceType.c_str(), deviceType.size() + 1);
	strncpy(theParams.DeviceInfoParams.type_, type.c_str(), type.size() + 1);

	UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kRedisSetDeviceRole);
	for (UInt32 currentModule = 0; currentModule < numModules;)
	{
		QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kRedisSetDeviceRole, currentModule);
		(void)theModule->CallDispatch(Easy_RedisSetDevice_Role, &theParams);
		break;
	}

	delete[] theParams.DeviceInfoParams.serial_;
	delete[] theParams.DeviceInfoParams.channels_;
	delete[] theParams.DeviceInfoParams.deviceType_;
	delete[] theParams.DeviceInfoParams.type_;
	delete[] theParams.DeviceInfoParams.token_;

}

/*
	1.��ȡTerminalType��AppType,�����߼���֤���������򷵻�400 httpBadRequest;
	2.��֤Serial��Token����Ȩ����֤���������򷵻�401 httpUnAuthorized;
	3.��ȡName��Tag��Ϣ���б��ر������д��Redis;
	4.�����APPTypeΪEasyNVR,��ȡChannelsͨ����Ϣ���ر������д��Redis
*/
QTSS_Error HTTPSession::execNetMsgDSRegisterReq(const char* json)
{
	QTSS_Error theErr = QTSS_NoErr;

	EasyMsgDSRegisterREQ regREQ(json);

	//update info each time
	if (!device_->GetDevInfo(json))
	{
		return  QTSS_BadArgument;
	}

	while (!fAuthenticated)
	{
		//1.��ȡTerminalType��AppType,�����߼���֤���������򷵻�400 httpBadRequest;
		int appType = regREQ.GetAppType();
		//int terminalType = regREQ.GetTerminalType();
		switch (appType)
		{
		case EASY_APP_TYPE_CAMERA:
			fSessionType = EasyCameraSession;
			//fTerminalType = terminalType;
			break;
		case EASY_APP_TYPE_NVR:
			fSessionType = EasyNVRSession;
			//fTerminalType = terminalType;
			break;
		default:
			break;
		}

		if (fSessionType >= EasyHTTPSession)
		{
			//�豸ע��Ȳ���EasyCamera��Ҳ����EasyNVR�����ش���
			theErr = QTSS_BadArgument;
			break;
		}

		//2.��֤Serial��Token����Ȩ����֤���������򷵻�401 httpUnAuthorized;
		string serial = regREQ.GetBodyValue(EASY_TAG_SERIAL);
		string token = regREQ.GetBodyValue(EASY_TAG_TOKEN);

		if (serial.empty())
		{
			theErr = QTSS_AttrDoesntExist;
			break;
		}

		//��֤Serial��Token�Ƿ�Ϸ�
		/*if (false)
		{
			theErr = QTSS_NotPreemptiveSafe;
			break;
		}*/

		auto deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
		auto regErr = deviceMap->Register(device_->serial_, this);
		if (regErr == OS_NoErr)
		{
			auto msgStr = Format("Device register��Device_serial[%s]\n", device_->serial_);
			QTSServerInterface::LogError(qtssMessageVerbosity, const_cast<char*>(msgStr.c_str()));

			addDevice();
			fAuthenticated = true;
		}
		else
		{
			//�豸��ͻ��ʱ��ǰһ���豸������,��Ϊ�ϵ硢��������������ǲ���Ͽ��ģ�����豸���硢����ͨ˳֮��ͻ������ͻ��
			//һ�����ӵĳ�ʱʱ90�룬Ҫ�ȵ�90��֮���豸��������ע�����ߡ�
			auto theDevRef = deviceMap->Resolve(device_->serial_);
			if (theDevRef)//�ҵ�ָ���豸
			{
				OSRefReleaserEx releaser(deviceMap, device_->serial_);
				HTTPSession* pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�Ự
				pDevSession->Signal(Task::kKillEvent);//��ֹ�豸����
			}
			//��һ����Ȼ�������߳�ͻ����Ϊ��Ȼ���豸������Task::kKillEvent��Ϣ�����豸���ܲ���������ֹ�������Ҫѭ���ȴ��Ƿ��Ѿ���ֹ��
			theErr = QTSS_AttrNameExists;;
		}
		break;
	}

	if (theErr != QTSS_NoErr)	return theErr;

	//�ߵ���˵�����豸�ɹ�ע���������
	EasyProtocol req(json);
	EasyProtocolACK rsp(MSG_SD_REGISTER_ACK);
	EasyJsonValue header, body;
	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_SERIAL] = device_->serial_;

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSFreeStreamReq(const char* json)//�ͻ��˵�ֱֹͣ������
{
	//�㷨����������ָ���豸�����豸���ڣ������豸����ֹͣ������
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/
	EasyProtocol req(json);

	string strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);
	string strChannel = req.GetBodyValue(EASY_TAG_CHANNEL);

	if (strDeviceSerial.empty())
	{
		return QTSS_BadArgument;
	}

	string strReserve = req.GetBodyValue(EASY_TAG_RESERVE);
	string strProtocol = req.GetBodyValue(EASY_TAG_PROTOCOL);

	//Ϊ��ѡ�������Ĭ��ֵ
	if (strChannel.empty())
		strChannel = "1";
	if (strReserve.empty())
		strReserve = "1";

	auto deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	auto theDevRef = deviceMap->Resolve(strDeviceSerial);
	if (!theDevRef)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, strDeviceSerial);
	//�ߵ���˵������ָ���豸������豸����ֹͣ��������
	auto pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�ػ�

	EasyProtocolACK reqreq(MSG_SD_STREAM_STOP_REQ);
	EasyJsonValue headerheader, bodybody;

	headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
	headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

	bodybody[EASY_TAG_SERIAL] = strDeviceSerial;
	bodybody[EASY_TAG_CHANNEL] = strChannel;
	bodybody[EASY_TAG_RESERVE] = strReserve;
	bodybody[EASY_TAG_PROTOCOL] = strProtocol;
	bodybody[EASY_TAG_FROM] = sessionId_;
	bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
	bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

	reqreq.SetHead(headerheader);
	reqreq.SetBody(bodybody);

	string buffer = reqreq.GetMsg();
	pDevSession->SendHTTPPacket(buffer, false, false);

	//ֱ�ӶԿͻ��ˣ�EasyDarWin)������ȷ��Ӧ
	EasyProtocolACK rsp(MSG_SC_FREE_STREAM_ACK);
	EasyJsonValue header, body;
	header[EASY_TAG_CSEQ] = req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_SERIAL] = strDeviceSerial;
	body[EASY_TAG_CHANNEL] = strChannel;
	body[EASY_TAG_RESERVE] = strReserve;
	body[EASY_TAG_PROTOCOL] = strProtocol;

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgDSStreamStopAck(const char* json) const
{
	if (!fAuthenticated)
	{
		return httpUnAuthorized;
	}

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSStartStreamReqRESTful(const char* queryString)//�ŵ�ProcessRequest���ڵ�״̬ȥ����������ѭ������
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	if (queryString == nullptr)
	{
		return QTSS_BadArgument;
	}

	string decQueryString = EasyUtil::Urldecode(queryString);

	QueryParamList parList(decQueryString);
	auto chSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�
	auto chChannel = parList.DoFindCGIValueForParam(EASY_TAG_L_CHANNEL);//��ȡͨ��
	auto chReserve = parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);

	if (!chSerial || string(chSerial).empty())
		return QTSS_BadArgument;

	if (!isRightChannel(chChannel))
		chChannel = "1";
	if (!chReserve)
		chReserve = "1";

	UInt32 strCSeq = GetCSeq();
	string service;

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = deviceMap->Resolve(chSerial);
	if (theDevRef == nullptr)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, chSerial);
	//�ߵ���˵������ָ���豸
	auto pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());

	auto deviceInfo = pDevSession->GetDeviceInfo();

	if (deviceInfo->eAppType == EASY_APP_TYPE_NVR)
	{
		auto& channels = deviceInfo->channels_;
		if (channels.find(chChannel) == channels.end())
		{
			return QTSS_BadArgument;
		}

		if (channels[chChannel].status_ == string("offline"))
		{
			return QTSS_BadArgument;
		}
	}

	string strDssIP, strHttpPort, strDssPort;
	char chDssIP[sIPSize] = { 0 };
	char chDssPort[sPortSize] = { 0 };
	char chHTTPPort[sPortSize] = { 0 };

	QTSS_RoleParams theParams;
	theParams.GetAssociatedDarwinParams.inSerial = const_cast<char*>(chSerial);
	theParams.GetAssociatedDarwinParams.inChannel = const_cast<char*>(chChannel);
	theParams.GetAssociatedDarwinParams.outDssIP = chDssIP;
	theParams.GetAssociatedDarwinParams.outHTTPPort = chHTTPPort;
	theParams.GetAssociatedDarwinParams.outDssPort = chDssPort;
	theParams.GetAssociatedDarwinParams.isOn = false;

	UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kRedisGetEasyDarwinRole);
	for (UInt32 currentModule = 0; currentModule < numModules; ++currentModule)
	{
		QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kRedisGetEasyDarwinRole, currentModule);
		(void)theModule->CallDispatch(Easy_RedisGetEasyDarwin_Role, &theParams);
	}

	int errorNo = EASY_ERROR_SUCCESS_OK;

	if (chDssIP[0] != 0)
	{
		strDssIP = chDssIP;
		strHttpPort = chHTTPPort;
		strDssPort = chDssPort;

		service = string("IP=") + strDssIP + ";Port=" + strHttpPort + ";Type=EasyDarwin";

		if (!theParams.GetAssociatedDarwinParams.isOn)
		{
			EasyProtocolACK reqreq(MSG_SD_PUSH_STREAM_REQ);
			EasyJsonValue headerheader, bodybody;

			headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());
			headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

			bodybody[EASY_TAG_SERVER_IP] = strDssIP;
			bodybody[EASY_TAG_SERVER_PORT] = strDssPort;
			bodybody[EASY_TAG_SERIAL] = chSerial;
			bodybody[EASY_TAG_CHANNEL] = chChannel;
			bodybody[EASY_TAG_RESERVE] = chReserve;
			bodybody[EASY_TAG_FROM] = sessionId_;
			bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
			bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

			darwinHttpPort_ = strHttpPort;

			reqreq.SetHead(headerheader);
			reqreq.SetBody(bodybody);

			string buffer = reqreq.GetMsg();
			pDevSession->SendHTTPPacket(buffer, false, false);

			fTimeoutTask.SetTimeout(3 * 1000);
			fTimeoutTask.RefreshTimeout();

			return QTSS_NoErr;
		}
	}
	else
	{
		errorNo = EASY_ERROR_SERVER_UNAVAILABLE;
	}

	//�ߵ���˵���Կͻ��˵���ȷ��Ӧ,��Ϊ�����Ӧֱ�ӷ��ء�
	EasyProtocolACK rsp(MSG_SC_START_STREAM_ACK);
	EasyJsonValue header, body;
	body[EASY_TAG_SERVICE] = service;
	body[EASY_TAG_SERIAL] = chSerial;
	body[EASY_TAG_CHANNEL] = chChannel;
	body[EASY_TAG_RESERVE] = chReserve;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = EasyUtil::ToString(strCSeq);
	header[EASY_TAG_ERROR_NUM] = errorNo;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(errorNo);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}


QTSS_Error HTTPSession::execNetMsgCSStopStreamReqRESTful(const char* queryString)//�ŵ�ProcessRequest���ڵ�״̬ȥ����������ѭ������
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	if (queryString == nullptr)
	{
		return QTSS_BadArgument;
	}

	string decQueryString = EasyUtil::Urldecode(queryString);

	QueryParamList parList(decQueryString);
	const char* strDeviceSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�
	const char* strChannel = parList.DoFindCGIValueForParam(EASY_TAG_L_CHANNEL);//��ȡͨ��
	const char* strReserve = parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);//

	//Ϊ��ѡ�������Ĭ��ֵ
	if (!isRightChannel(strChannel))
		strChannel = "1";
	if (strReserve == nullptr)
		strReserve = "1";

	if (strDeviceSerial == nullptr)
		return QTSS_BadArgument;

	auto deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	auto theDevRef = deviceMap->Resolve(strDeviceSerial);
	if (theDevRef == nullptr)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, strDeviceSerial);
	//�ߵ���˵������ָ���豸������豸����ֹͣ��������
	HTTPSession* pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�ػ�

	EasyProtocolACK reqreq(MSG_SD_STREAM_STOP_REQ);
	EasyJsonValue headerheader, bodybody;

	headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
	headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

	bodybody[EASY_TAG_SERIAL] = strDeviceSerial;
	bodybody[EASY_TAG_CHANNEL] = strChannel;
	bodybody[EASY_TAG_RESERVE] = strReserve;
	bodybody[EASY_TAG_PROTOCOL] = "";
	bodybody[EASY_TAG_FROM] = sessionId_;
	bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
	bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

	reqreq.SetHead(headerheader);
	reqreq.SetBody(bodybody);

	string buffer = reqreq.GetMsg();
	pDevSession->SendHTTPPacket(buffer, false, false);

	//ֱ�ӶԿͻ��ˣ�EasyDarWin)������ȷ��Ӧ
	EasyProtocolACK rsp(MSG_SC_STOP_STREAM_ACK);
	EasyJsonValue header, body;
	header[EASY_TAG_CSEQ] = EasyUtil::ToString(GetCSeq());
	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_SERIAL] = strDeviceSerial;
	body[EASY_TAG_CHANNEL] = strChannel;
	body[EASY_TAG_RESERVE] = strReserve;

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}


QTSS_Error HTTPSession::execNetMsgDSPushStreamAck(const char* json) const
{
	if (!fAuthenticated)//û�н�����֤����
		return httpUnAuthorized;

	//�����豸��������Ӧ�ǲ���Ҫ�ڽ��л�Ӧ�ģ�ֱ�ӽ����ҵ���Ӧ�Ŀͻ���Session����ֵ����	
	EasyProtocol req(json);

	string strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	string strChannel = req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	string strProtocol = req.GetBodyValue(EASY_TAG_PROTOCOL);//Э��,�ն˽�֧��RTSP����
	string strReserve = req.GetBodyValue(EASY_TAG_RESERVE);//������
	string strDssIP = req.GetBodyValue(EASY_TAG_SERVER_IP);//�豸ʵ��������ַ
	string strDssPort = req.GetBodyValue(EASY_TAG_SERVER_PORT);//�Ͷ˿�
	string strFrom = req.GetBodyValue(EASY_TAG_FROM);
	string strTo = req.GetBodyValue(EASY_TAG_TO);
	string strVia = req.GetBodyValue(EASY_TAG_VIA);

	string strCSeq = req.GetHeaderValue(EASY_TAG_CSEQ);//����ǹؼ���
	string strStateCode = req.GetHeaderValue(EASY_TAG_ERROR_NUM);//״̬��

	if (strChannel.empty())
		strChannel = "1";
	if (strReserve.empty())
		strReserve = "1";

	OSRefTableEx* sessionMap = QTSServerInterface::GetServer()->GetHTTPSessionMap();
	OSRefTableEx::OSRefEx* sessionRef = sessionMap->Resolve(strTo);
	if (sessionRef == nullptr)
		return EASY_ERROR_SESSION_NOT_FOUND;

	OSRefReleaserEx releaser(sessionMap, strTo);
	HTTPSession* httpSession = static_cast<HTTPSession*>(sessionRef->GetObjectPtr());

	if (httpSession->IsLiveSession())
	{
		string service = string("IP=") + strDssIP + ";Port=" + httpSession->GetDarwinHTTPPort() + ";Type=EasyDarwin";

		//�ߵ���˵���Կͻ��˵���ȷ��Ӧ,��Ϊ�����Ӧֱ�ӷ��ء�
		EasyProtocolACK rsp(MSG_SC_START_STREAM_ACK);
		EasyJsonValue header, body;
		body[EASY_TAG_SERVICE] = service;
		body[EASY_TAG_SERIAL] = strDeviceSerial;
		body[EASY_TAG_CHANNEL] = strChannel;
		body[EASY_TAG_RESERVE] = strReserve;

		header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
		header[EASY_TAG_CSEQ] = strCSeq;
		header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
		header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

		rsp.SetHead(header);
		rsp.SetBody(body);

		string msg = rsp.GetMsg();
		httpSession->SendHTTPPacket(msg, false, false);
	}

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetDeviceListReqRESTful(const char* queryString)//�ͻ��˻���豸�б�
{

	//if (!fAuthenticated)//û�н�����֤����
	//	return httpUnAuthorized;

	std::string queryTemp;
	if (queryString != nullptr)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}
	QueryParamList parList(queryTemp);
	const char* chAppType = parList.DoFindCGIValueForParam(EASY_TAG_APP_TYPE);//APPType
	const char* chTerminalType = parList.DoFindCGIValueForParam(EASY_TAG_TERMINAL_TYPE);//TerminalType

	EasyProtocolACK rsp(MSG_SC_DEVICE_LIST_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	unordered_set<string> terminalSet;
	if (chTerminalType != nullptr)
	{
		string terminalTemp(chTerminalType);

		if (boost::ends_with(terminalTemp, "|"))
		{
			boost::erase_tail(terminalTemp, 1);
		}
		boost::split(terminalSet, terminalTemp, boost::is_any_of("|"), boost::token_compress_on);
	}

	OSMutex* mutexMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMutex();
	OSHashMap* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap()->GetMap();
	Json::Value* proot = rsp.GetRoot();

	{
		OSMutexLocker lock(mutexMap);
		int iDevNum = 0;

		for (auto itRef = deviceMap->begin(); itRef != deviceMap->end(); ++itRef)
		{
			auto deviceInfo = static_cast<HTTPSession*>(itRef->second->GetObjectPtr())->GetDeviceInfo();
			if (chAppType != nullptr)// AppType fileter
			{
				if (EasyProtocol::GetAppTypeString(deviceInfo->eAppType) != string(chAppType))
					continue;
			}
			if (chTerminalType != nullptr)// TerminateType fileter
			{
				if (terminalSet.find(EasyProtocol::GetTerminalTypeString(deviceInfo->eDeviceType)) == terminalSet.end())
					continue;
			}

			iDevNum++;

			Json::Value value;
			value[EASY_TAG_SERIAL] = deviceInfo->serial_;//����ط������˱���,deviceMap�������ݣ�����deviceInfo�������ݶ��ǿ�
			value[EASY_TAG_NAME] = deviceInfo->name_;
			value[EASY_TAG_TAG] = deviceInfo->tag_;
			value[EASY_TAG_APP_TYPE] = EasyProtocol::GetAppTypeString(deviceInfo->eAppType);
			value[EASY_TAG_TERMINAL_TYPE] = EasyProtocol::GetTerminalTypeString(deviceInfo->eDeviceType);
			//����豸��EasyCamera,�򷵻��豸������Ϣ
			if (deviceInfo->eAppType == EASY_APP_TYPE_CAMERA)
			{
				value[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
			}
			(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_DEVICES].append(value);
		}
		body[EASY_TAG_DEVICE_COUNT] = iDevNum;
	}

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSDeviceListReq(const char* json)//�ͻ��˻���豸�б�
{
	/*
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/
	EasyProtocol req(json);

	EasyProtocolACK rsp(MSG_SC_DEVICE_LIST_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSMutex* mutexMap = deviceMap->GetMutex();
	OSHashMap* deviceHashMap = deviceMap->GetMap();
	Json::Value* proot = rsp.GetRoot();

	{
		OSMutexLocker lock(mutexMap);
		body[EASY_TAG_DEVICE_COUNT] = deviceMap->GetEleNumInMap();
		for (auto itRef = deviceHashMap->begin(); itRef != deviceHashMap->end(); ++itRef)
		{
			Json::Value value;
			auto deviceInfo = static_cast<HTTPSession*>(itRef->second->GetObjectPtr())->GetDeviceInfo();
			value[EASY_TAG_SERIAL] = deviceInfo->serial_;
			value[EASY_TAG_NAME] = deviceInfo->name_;
			value[EASY_TAG_TAG] = deviceInfo->tag_;
			value[EASY_TAG_APP_TYPE] = EasyProtocol::GetAppTypeString(deviceInfo->eAppType);
			value[EASY_TAG_TERMINAL_TYPE] = EasyProtocol::GetTerminalTypeString(deviceInfo->eDeviceType);
			//����豸��EasyCamera,�򷵻��豸������Ϣ
			if (deviceInfo->eAppType == EASY_APP_TYPE_CAMERA)
			{
				value[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
			}
			(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_DEVICES].append(value);
		}
	}

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetCameraListReqRESTful(const char* queryString)
{
	/*
		if(!fAuthenticated)//û�н�����֤����
		return httpUnAuthorized;
	*/
	if (queryString == nullptr)
	{
		return QTSS_BadArgument;
	}

	string decQueryString = EasyUtil::Urldecode(queryString);

	QueryParamList parList(decQueryString);

	const char* device_serial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�

	if (device_serial == nullptr)
		return QTSS_BadArgument;

	EasyProtocolACK rsp(MSG_SC_DEVICE_INFO_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;

	body[EASY_TAG_SERIAL] = device_serial;

	auto deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	auto theDevRef = deviceMap->Resolve(device_serial);
	if (theDevRef == nullptr)//������ָ���豸
	{
		header[EASY_TAG_ERROR_NUM] = EASY_ERROR_DEVICE_NOT_FOUND;
		header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_DEVICE_NOT_FOUND);
	}
	else//����ָ���豸�����ȡ����豸������ͷ��Ϣ
	{
		OSRefReleaserEx releaser(deviceMap, device_serial);

		header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
		header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

		auto proot = rsp.GetRoot();
		auto deviceInfo = static_cast<HTTPSession*>(theDevRef->GetObjectPtr())->GetDeviceInfo();
		if (deviceInfo->eAppType == EASY_APP_TYPE_CAMERA)
		{
			body[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
		}
		else
		{
			body[EASY_TAG_CHANNEL_COUNT] = deviceInfo->channelCount_;
			for (auto itCam = deviceInfo->channels_.begin(); itCam != deviceInfo->channels_.end(); ++itCam)
			{
				Json::Value value;
				value[EASY_TAG_CHANNEL] = itCam->second.channel_;
				value[EASY_TAG_NAME] = itCam->second.name_;
				value[EASY_TAG_STATUS] = itCam->second.status_;
				value[EASY_TAG_SNAP_URL] = itCam->second.snapJpgPath_;
				(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_CHANNELS].append(value);
			}
		}

	}
	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSCameraListReq(const char* json)
{
	/*
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/
	EasyProtocol req(json);
	auto strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);

	if (strDeviceSerial.empty())
		return QTSS_BadArgument;

	EasyProtocolACK rsp(MSG_SC_DEVICE_INFO_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = req.GetHeaderValue(EASY_TAG_CSEQ);
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);
	body[EASY_TAG_SERIAL] = strDeviceSerial;

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = deviceMap->Resolve(strDeviceSerial);
	if (theDevRef == nullptr)//������ָ���豸
	{
		return EASY_ERROR_DEVICE_NOT_FOUND;//�������������Ĵ���
	}
	//����ָ���豸�����ȡ����豸������ͷ��Ϣ
	OSRefReleaserEx releaser(deviceMap, strDeviceSerial);

	Json::Value* proot = rsp.GetRoot();
	auto deviceInfo = static_cast<HTTPSession*>(theDevRef->GetObjectPtr())->GetDeviceInfo();
	if (deviceInfo->eAppType == EASY_APP_TYPE_CAMERA)
	{
		body[EASY_TAG_SNAP_URL] = deviceInfo->snapJpgPath_;
	}
	else
	{
		body[EASY_TAG_CHANNEL_COUNT] = static_cast<HTTPSession*>(theDevRef->GetObjectPtr())->GetDeviceInfo()->channelCount_;
		for (auto itCam = deviceInfo->channels_.begin(); itCam != deviceInfo->channels_.end(); ++itCam)
		{
			Json::Value value;
			value[EASY_TAG_CHANNEL] = itCam->second.channel_;
			value[EASY_TAG_NAME] = itCam->second.name_;
			value[EASY_TAG_STATUS] = itCam->second.status_;
			body[EASY_TAG_SNAP_URL] = itCam->second.snapJpgPath_;
			(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_CHANNELS].append(value);
		}
	}
	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::processRequest()//��������
{
	//OSCharArrayDeleter charArrayPathDeleter(theRequestBody);//��Ҫ����ɾ������Ϊ����ִ�ж�Σ�����������Ĵ�����Ϻ��ٽ���ɾ��

	if (fRequestBody == nullptr)//��ʾû����ȷ�Ľ�������SetUpRequest����û�н��������ݲ���
		return QTSS_NoErr;

	//��Ϣ����
	QTSS_Error theErr;
	EasyProtocol protocol(fRequestBody);
	int nNetMsg = protocol.GetMessageType(), nRspMsg = MSG_SC_EXCEPTION;

	switch (nNetMsg)
	{
	case MSG_DS_REGISTER_REQ://�����豸������Ϣ,�豸���Ͱ���NVR������ͷ����������
		theErr = execNetMsgDSRegisterReq(fRequestBody);
		nRspMsg = MSG_SD_REGISTER_ACK;
		break;
	case MSG_DS_PUSH_STREAM_ACK://�豸�Ŀ�ʼ����Ӧ
		theErr = execNetMsgDSPushStreamAck(fRequestBody);
		nRspMsg = MSG_DS_PUSH_STREAM_ACK;//ע�⣬����ʵ�����ǲ�Ӧ���ٻ�Ӧ��
		break;
	case MSG_CS_FREE_STREAM_REQ://�ͻ��˵�ֱֹͣ������
		theErr = execNetMsgCSFreeStreamReq(fRequestBody);
		nRspMsg = MSG_SC_FREE_STREAM_ACK;
		break;
	case MSG_DS_STREAM_STOP_ACK://�豸��EasyCMS��ֹͣ������Ӧ
		theErr = execNetMsgDSStreamStopAck(fRequestBody);
		nRspMsg = MSG_DS_STREAM_STOP_ACK;//ע�⣬����ʵ�����ǲ���Ҫ�ڽ��л�Ӧ��
		break;
	case MSG_CS_DEVICE_LIST_REQ://�豸�б�����
		theErr = execNetMsgCSDeviceListReq(fRequestBody);//add
		nRspMsg = MSG_SC_DEVICE_LIST_ACK;
		break;
	case MSG_CS_DEVICE_INFO_REQ://����ͷ�б�����,�豸�ľ�����Ϣ
		theErr = execNetMsgCSCameraListReq(fRequestBody);//add
		nRspMsg = MSG_SC_DEVICE_INFO_ACK;
		break;
	case MSG_DS_POST_SNAP_REQ://�豸�����ϴ�
		theErr = execNetMsgDSPostSnapReq(fRequestBody);
		nRspMsg = MSG_SD_POST_SNAP_ACK;
		break;
	case MSG_DS_CONTROL_PTZ_ACK:
		theErr = execNetMsgDSPTZControlAck(fRequestBody);
		nRspMsg = MSG_DS_CONTROL_PTZ_ACK;
		break;
	case MSG_DS_CONTROL_PRESET_ACK:
		theErr = execNetMsgDSPresetControlAck(fRequestBody);
		nRspMsg = MSG_DS_CONTROL_PRESET_ACK;
		break;
	case MSG_CS_TALKBACK_CONTROL_REQ:
		theErr = execNetMsgCSTalkbackControlReq(fRequestBody);
		nRspMsg = MSG_SC_TALKBACK_CONTROL_ACK;
		break;
	case MSG_DS_CONTROL_TALKBACK_ACK:
		theErr = execNetMSGDSTalkbackControlAck(fRequestBody);
		nRspMsg = MSG_DS_CONTROL_TALKBACK_ACK;
		break;
	default:
		theErr = execNetMsgErrorReqHandler(httpNotImplemented);
		break;
	}

	//��������������Զ�������һ��Ҫ����QTSS_NoErr
	if (theErr != QTSS_NoErr)//��������ȷ��Ӧ���ǵȴ����ض���QTSS_NoErr�����ִ��󣬶Դ������ͳһ��Ӧ
	{
		EasyProtocol req(fRequestBody);
		EasyProtocolACK rsp(nRspMsg);
		EasyJsonValue header;
		header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
		header[EASY_TAG_CSEQ] = req.GetHeaderValue(EASY_TAG_CSEQ);

		switch (theErr)
		{
		case QTSS_BadArgument:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_CLIENT_BAD_REQUEST;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_BAD_REQUEST);
			break;
		case httpUnAuthorized:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_CLIENT_UNAUTHORIZED;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_UNAUTHORIZED);
			break;
		case QTSS_AttrNameExists:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_CONFLICT;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_CONFLICT);
			break;
		case EASY_ERROR_DEVICE_NOT_FOUND:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_DEVICE_NOT_FOUND;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_DEVICE_NOT_FOUND);
			break;
		case EASY_ERROR_SERVICE_NOT_FOUND:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SERVICE_NOT_FOUND;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SERVICE_NOT_FOUND);
			break;
		case httpRequestTimeout:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_REQUEST_TIMEOUT;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_REQUEST_TIMEOUT);
			break;
		case EASY_ERROR_SERVER_INTERNAL_ERROR:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SERVER_INTERNAL_ERROR;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SERVER_INTERNAL_ERROR);
			break;
		case EASY_ERROR_SERVER_NOT_IMPLEMENTED:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SERVER_NOT_IMPLEMENTED;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SERVER_NOT_IMPLEMENTED);
			break;
		default:
			header[EASY_TAG_ERROR_NUM] = EASY_ERROR_CLIENT_BAD_REQUEST;
			header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_BAD_REQUEST);
			break;
		}

		rsp.SetHead(header);

		string msg = rsp.GetMsg();
		this->SendHTTPPacket(msg, false, false);
	}
	return theErr;
}

QTSS_Error HTTPSession::execNetMsgCSPTZControlReqRESTful(const char* queryString)
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	if (queryString == nullptr)
	{
		return QTSS_BadArgument;
	}

	string decQueryString = EasyUtil::Urldecode(queryString);

	QueryParamList parList(decQueryString);

	const char* chSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�
	const char* chChannel = parList.DoFindCGIValueForParam(EASY_TAG_L_CHANNEL);//��ȡͨ��
	const char* chProtocol = parList.DoFindCGIValueForParam(EASY_TAG_L_PROTOCOL);//��ȡͨ��
	const char* chReserve(parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE));//��ȡͨ��
	const char* chActionType = parList.DoFindCGIValueForParam(EASY_TAG_L_ACTION_TYPE);
	const char* chCmd = parList.DoFindCGIValueForParam(EASY_TAG_L_CMD);
	const char* chSpeed = parList.DoFindCGIValueForParam(EASY_TAG_L_SPEED);

	if (!chSerial || !chProtocol || !chActionType || !chCmd)
		return QTSS_BadArgument;

	//Ϊ��ѡ�������Ĭ��ֵ
	if (!isRightChannel(chChannel))
		chChannel = "1";
	if (chReserve == nullptr)
		chReserve = "1";

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = deviceMap->Resolve(chSerial);
	if (theDevRef == nullptr)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, chSerial);
	//�ߵ���˵������ָ���豸
	HTTPSession* pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�ػ�

	EasyProtocolACK reqreq(MSG_SD_CONTROL_PTZ_REQ);
	EasyJsonValue headerheader, bodybody;

	UInt32 strCSEQ = pDevSession->GetCSeq();
	headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(strCSEQ);
	headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

	string strProtocol(chProtocol);
	string strActionType(chActionType);
	string strCmd(chCmd);
	boost::to_upper(strProtocol);
	boost::to_upper(strActionType);
	boost::to_upper(strCmd);

	bodybody[EASY_TAG_SERIAL] = chSerial;
	bodybody[EASY_TAG_CHANNEL] = chChannel;
	bodybody[EASY_TAG_PROTOCOL] = strProtocol;
	bodybody[EASY_TAG_RESERVE] = chReserve;
	bodybody[EASY_TAG_ACTION_TYPE] = strActionType;
	bodybody[EASY_TAG_CMD] = strCmd;
	bodybody[EASY_TAG_SPEED] = chSpeed;
	bodybody[EASY_TAG_FROM] = sessionId_;
	bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
	bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

	reqreq.SetHead(headerheader);
	reqreq.SetBody(bodybody);

	string buffer = reqreq.GetMsg();
	pDevSession->SendHTTPPacket(buffer, false, false);

	EasyProtocolACK rsp(MSG_SC_PTZ_CONTROL_ACK);
	EasyJsonValue header, body;
	body[EASY_TAG_SERIAL] = chSerial;
	body[EASY_TAG_CHANNEL] = chChannel;
	body[EASY_TAG_PROTOCOL] = strProtocol;
	body[EASY_TAG_RESERVE] = chReserve;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = EasyUtil::ToString(strCSEQ);
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgDSPTZControlAck(const char* json)
{
	//  if (!fAuthenticated)//û�н�����֤����
	//      return httpUnAuthorized;

	//  //�����豸��������Ӧ�ǲ���Ҫ�ڽ��л�Ӧ�ģ�ֱ�ӽ����ҵ���Ӧ�Ŀͻ���Session����ֵ����	
	//  EasyProtocol req(json);

	//  string strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	//  string strChannel = req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	//  string strProtocol = req.GetBodyValue(EASY_TAG_PROTOCOL);//Э��,�ն˽�֧��RTSP����
	//  string strReserve = req.GetBodyValue(EASY_TAG_RESERVE);//������
	//  string strFrom = req.GetBodyValue(EASY_TAG_FROM);
	//  string strTo = req.GetBodyValue(EASY_TAG_TO);
	//  string strVia = req.GetBodyValue(EASY_TAG_VIA);

	//  string strCSeq = req.GetHeaderValue(EASY_TAG_CSEQ);//����ǹؼ���
	//  string strStateCode = req.GetHeaderValue(EASY_TAG_ERROR_NUM);//״̬��

	//  if (strChannel.empty())
	//      strChannel = "1";
	//  if (strReserve.empty())
	//      strReserve = "1";

	//  OSRefTableEx* sessionMap = QTSServerInterface::GetServer()->GetHTTPSessionMap();
	//  OSRefTableEx::OSRefEx* sessionRef = sessionMap->Resolve(strTo);
	//  if (sessionRef == nullptr)
	//      return EASY_ERROR_SESSION_NOT_FOUND;

	//  OSRefReleaserEx releaser(sessionMap, strTo);
	//  HTTPSession* httpSession = static_cast<HTTPSession*>(sessionRef->GetObjectPtr());

	//  if (httpSession->IsLiveSession())
	//  {
	//      //�ߵ���˵���Կͻ��˵���ȷ��Ӧ,��Ϊ�����Ӧֱ�ӷ��ء�
	//      EasyProtocolACK rsp(MSG_SC_PTZ_CONTROL_ACK);
	//      EasyJsonValue header, body;
	//      body[EASY_TAG_SERIAL] = strDeviceSerial;
	//      body[EASY_TAG_CHANNEL] = strChannel;
	//      body[EASY_TAG_PROTOCOL] = strProtocol;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������
	//      body[EASY_TAG_RESERVE] = strReserve;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������

	//      header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	//      header[EASY_TAG_CSEQ] = strCSeq;
	//      header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	//      header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	//      rsp.SetHead(header);
	//      rsp.SetBody(body);
	//      string msg = rsp.GetMsg();
		  //httpSession->ProcessEvent(msg, httpResponseType);
	//  }

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSPresetControlReqRESTful(const char* queryString)
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

	if (queryString == nullptr)
	{
		return QTSS_BadArgument;
	}

	string decQueryString = EasyUtil::Urldecode(queryString);

	QueryParamList parList(decQueryString);

	const char* chSerial = parList.DoFindCGIValueForParam(EASY_TAG_L_DEVICE);//��ȡ�豸���к�
	const char* chChannel = parList.DoFindCGIValueForParam(EASY_TAG_L_CHANNEL);//��ȡͨ��
	const char* chProtocol = parList.DoFindCGIValueForParam(EASY_TAG_L_PROTOCOL);//��ȡͨ��
	const char* chReserve = parList.DoFindCGIValueForParam(EASY_TAG_L_RESERVE);//��ȡͨ��
	const char* chCmd = parList.DoFindCGIValueForParam(EASY_TAG_L_CMD);
	const char* chPreset = parList.DoFindCGIValueForParam(EASY_TAG_L_PRESET);

	if (!chSerial || !chProtocol || !chCmd)
		return QTSS_BadArgument;

	//Ϊ��ѡ�������Ĭ��ֵ
	if (!isRightChannel(chChannel))
		chChannel = "1";
	if (chReserve == nullptr)
		chReserve = "1";

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = deviceMap->Resolve(chSerial);
	if (theDevRef == nullptr)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, chSerial);
	//�ߵ���˵������ָ���豸
	HTTPSession* pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�ػ�

	EasyProtocolACK reqreq(MSG_SD_CONTROL_PRESET_REQ);
	EasyJsonValue headerheader, bodybody;

	headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());
	headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

	bodybody[EASY_TAG_SERIAL] = chSerial;
	bodybody[EASY_TAG_CHANNEL] = chChannel;
	bodybody[EASY_TAG_PROTOCOL] = chProtocol;
	bodybody[EASY_TAG_RESERVE] = chReserve;
	bodybody[EASY_TAG_CMD] = chCmd;
	bodybody[EASY_TAG_PRESET] = chPreset;
	bodybody[EASY_TAG_FROM] = sessionId_;
	bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
	bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

	reqreq.SetHead(headerheader);
	reqreq.SetBody(bodybody);

	string buffer = reqreq.GetMsg();
	pDevSession->SendHTTPPacket(buffer, false, false);

	fTimeoutTask.SetTimeout(3 * 1000);
	fTimeoutTask.RefreshTimeout();

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgDSPresetControlAck(const char* json) const
{
	if (!fAuthenticated)//û�н�����֤����
		return httpUnAuthorized;

	//�����豸��������Ӧ�ǲ���Ҫ�ڽ��л�Ӧ�ģ�ֱ�ӽ����ҵ���Ӧ�Ŀͻ���Session����ֵ����	
	EasyProtocol req(json);

	string strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);//�豸���к�
	string strChannel = req.GetBodyValue(EASY_TAG_CHANNEL);//����ͷ���к�
	string strProtocol = req.GetBodyValue(EASY_TAG_PROTOCOL);//Э��,�ն˽�֧��RTSP����
	string strReserve = req.GetBodyValue(EASY_TAG_RESERVE);//������
	string strFrom = req.GetBodyValue(EASY_TAG_FROM);
	string strTo = req.GetBodyValue(EASY_TAG_TO);
	string strVia = req.GetBodyValue(EASY_TAG_VIA);

	string strCSeq = req.GetHeaderValue(EASY_TAG_CSEQ);//����ǹؼ���
	string strStateCode = req.GetHeaderValue(EASY_TAG_ERROR_NUM);//״̬��

	if (strChannel.empty())
		strChannel = "1";
	if (strReserve.empty())
		strReserve = "1";

	OSRefTableEx* sessionMap = QTSServerInterface::GetServer()->GetHTTPSessionMap();
	OSRefTableEx::OSRefEx* sessionRef = sessionMap->Resolve(strTo);
	if (sessionRef == nullptr)
		return EASY_ERROR_SESSION_NOT_FOUND;

	OSRefReleaserEx releaser(sessionMap, strTo);
	HTTPSession* httpSession = static_cast<HTTPSession*>(sessionRef->GetObjectPtr());

	if (httpSession->IsLiveSession())
	{
		//�ߵ���˵���Կͻ��˵���ȷ��Ӧ,��Ϊ�����Ӧֱ�ӷ��ء�
		EasyProtocolACK rsp(MSG_SC_PRESET_CONTROL_ACK);
		EasyJsonValue header, body;
		body[EASY_TAG_SERIAL] = strDeviceSerial;
		body[EASY_TAG_CHANNEL] = strChannel;
		body[EASY_TAG_PROTOCOL] = strProtocol;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������
		body[EASY_TAG_RESERVE] = strReserve;//�����ǰ�Ѿ��������򷵻�����ģ����򷵻�ʵ����������

		header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
		header[EASY_TAG_CSEQ] = strCSeq;
		header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
		header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

		rsp.SetHead(header);
		rsp.SetBody(body);

		string msg = rsp.GetMsg();
		httpSession->SendHTTPPacket(msg, false, false);
	}

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSTalkbackControlReq(const char* json)
{
	//if (!fAuthenticated)//û�н�����֤����
	//	return httpUnAuthorized;

	EasyProtocol req(json);

	string strDeviceSerial = req.GetBodyValue(EASY_TAG_SERIAL);
	string strChannel = req.GetBodyValue(EASY_TAG_CHANNEL);
	string strProtocol = req.GetBodyValue(EASY_TAG_PROTOCOL);
	string strReserve = req.GetBodyValue(EASY_TAG_RESERVE);
	string strCmd = req.GetBodyValue(EASY_TAG_CMD);
	string strAudioType = req.GetBodyValue(EASY_TAG_AUDIO_TYPE);
	string strAudioData = req.GetBodyValue(EASY_TAG_AUDIO_DATA);
	string strPts = req.GetBodyValue(EASY_TAG_PTS);

	string strCSeq = req.GetHeaderValue(EASY_TAG_CSEQ);//����ǹؼ���

	if (strChannel.empty())
		strChannel = "1";
	if (strReserve.empty())
		strReserve = "1";

	OSRefTableEx* deviceMap = QTSServerInterface::GetServer()->GetDeviceSessionMap();
	OSRefTableEx::OSRefEx* theDevRef = deviceMap->Resolve(strDeviceSerial);
	if (theDevRef == nullptr)//�Ҳ���ָ���豸
		return EASY_ERROR_DEVICE_NOT_FOUND;

	OSRefReleaserEx releaser(deviceMap, strDeviceSerial);
	//�ߵ���˵������ָ���豸
	HTTPSession* pDevSession = static_cast<HTTPSession*>(theDevRef->GetObjectPtr());//��õ�ǰ�豸�ػ�

	string errNo, errString;
	if (strCmd == "SENDDATA")
	{
		if (!pDevSession->GetTalkbackSession().empty() && pDevSession->GetTalkbackSession() == sessionId_)
		{
			EasyProtocolACK reqreq(MSG_SD_CONTROL_TALKBACK_REQ);
			EasyJsonValue headerheader, bodybody;

			headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
			headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

			bodybody[EASY_TAG_SERIAL] = strDeviceSerial;
			bodybody[EASY_TAG_CHANNEL] = strChannel;
			bodybody[EASY_TAG_PROTOCOL] = strProtocol;
			bodybody[EASY_TAG_RESERVE] = strReserve;
			bodybody[EASY_TAG_CMD] = strCmd;
			bodybody[EASY_TAG_AUDIO_TYPE] = strAudioType;
			bodybody[EASY_TAG_AUDIO_DATA] = strAudioData;
			bodybody[EASY_TAG_PTS] = strPts;
			bodybody[EASY_TAG_FROM] = sessionId_;
			bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
			bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

			reqreq.SetHead(headerheader);
			reqreq.SetBody(bodybody);

			string buffer = reqreq.GetMsg();
			pDevSession->SendHTTPPacket(buffer, false, false);

			errNo = EASY_ERROR_SUCCESS_OK;
			errString = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);
		}
		else
		{
			errNo = EASY_ERROR_CLIENT_BAD_REQUEST;
			errString = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_BAD_REQUEST);
			goto ACK;
		}
	}
	else
	{
		if (strCmd == "START")
		{
			if (pDevSession->GetTalkbackSession().empty())
			{
				pDevSession->SetTalkbackSession(sessionId_);
				errNo = EASY_ERROR_SUCCESS_OK;
				errString = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);
			}
			else if (pDevSession->GetTalkbackSession() == sessionId_)
			{
			}
			else
			{
				errNo = EASY_ERROR_CLIENT_BAD_REQUEST;
				errString = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_BAD_REQUEST);
				goto ACK;
			}
		}
		else if (strCmd == "STOP")
		{
			if (pDevSession->GetTalkbackSession().empty() || pDevSession->GetTalkbackSession() != sessionId_)
			{
				errNo = EASY_ERROR_CLIENT_BAD_REQUEST;
				errString = EasyProtocol::GetErrorString(EASY_ERROR_CLIENT_BAD_REQUEST);
				goto ACK;
			}

			pDevSession->SetTalkbackSession("");
			errNo = EASY_ERROR_SUCCESS_OK;
			errString = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);
		}

		EasyProtocolACK reqreq(MSG_SD_CONTROL_TALKBACK_REQ);
		EasyJsonValue headerheader, bodybody;

		headerheader[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
		headerheader[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;

		bodybody[EASY_TAG_SERIAL] = strDeviceSerial;
		bodybody[EASY_TAG_CHANNEL] = strChannel;
		bodybody[EASY_TAG_PROTOCOL] = strProtocol;
		bodybody[EASY_TAG_RESERVE] = strReserve;
		bodybody[EASY_TAG_CMD] = strCmd;
		bodybody[EASY_TAG_AUDIO_TYPE] = strAudioType;
		bodybody[EASY_TAG_AUDIO_DATA] = strAudioData;
		bodybody[EASY_TAG_PTS] = strPts;
		bodybody[EASY_TAG_FROM] = sessionId_;
		bodybody[EASY_TAG_TO] = pDevSession->GetValue(EasyHTTPSessionID)->GetAsCString();
		bodybody[EASY_TAG_VIA] = QTSServerInterface::GetServer()->GetCloudServiceNodeID();

		reqreq.SetHead(headerheader);
		reqreq.SetBody(bodybody);

		string buffer = reqreq.GetMsg();
		pDevSession->SendHTTPPacket(buffer, false, false);
	}

ACK:

	EasyProtocolACK rsp(MSG_SC_TALKBACK_CONTROL_ACK);
	EasyJsonValue header, body;
	body[EASY_TAG_SERIAL] = strDeviceSerial;
	body[EASY_TAG_CHANNEL] = strChannel;
	body[EASY_TAG_PROTOCOL] = strProtocol;
	body[EASY_TAG_RESERVE] = strReserve;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = EasyUtil::ToString(pDevSession->GetCSeq());
	header[EASY_TAG_ERROR_NUM] = errNo;
	header[EASY_TAG_ERROR_STRING] = errString;

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMSGDSTalkbackControlAck(const char* json)
{
	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSGetBaseConfigReqRESTful(const char* queryString)
{
	//if (!fAuthenticated)//û�н�����֤����
	//	return httpUnAuthorized;

	EasyProtocolACK rsp(MSG_SC_SERVER_BASE_CONFIG_ACK);
	EasyJsonValue header, body;

	header[EASY_TAG_VERSION] = EASY_PROTOCOL_VERSION;
	header[EASY_TAG_CSEQ] = 1;
	header[EASY_TAG_ERROR_NUM] = EASY_ERROR_SUCCESS_OK;
	header[EASY_TAG_ERROR_STRING] = EasyProtocol::GetErrorString(EASY_ERROR_SUCCESS_OK);

	body[EASY_TAG_CONFIG_SERVICE_WAN_IP] = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP();
	body[EASY_TAG_CONFIG_SERVICE_LAN_PORT] = QTSServerInterface::GetServer()->GetPrefs()->GetServiceLANPort();
	body[EASY_TAG_CONFIG_SERVICE_WAN_PORT] = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANPort();
	body[EASY_TAG_CONFIG_SNAP_LOCAL_PATH] = QTSServerInterface::GetServer()->GetPrefs()->GetSnapLocalPath();
	body[EASY_TAG_CONFIG_SNAP_WEB_PATH] = QTSServerInterface::GetServer()->GetPrefs()->GetSnapWebPath();

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSSetBaseConfigReqRESTful(const char* queryString)
{
	//if (!fAuthenticated)//û�н�����֤����
	//	return httpUnAuthorized;

	string queryTemp;
	if (queryString)
	{
		queryTemp = EasyUtil::Urldecode(queryString);
	}

	QueryParamList parList(queryTemp);
	const char* chWanIP = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_WAN_IP);
	if (chWanIP)
	{
		QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsServiceWANIPAddr, 0, chWanIP, strlen(chWanIP));
	}

	const char* chHTTPLanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_LAN_PORT);
	if (chHTTPLanPort)
	{
		UInt16 uHTTPLanPort = stoi(chHTTPLanPort);
		QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsServiceLANPort, 0, &uHTTPLanPort, sizeof(uHTTPLanPort));
	}

	const char*	chHTTPWanPort = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SERVICE_WAN_PORT);
	if (chHTTPWanPort)
	{
		UInt16 uHTTPWanPort = stoi(chHTTPWanPort);
		QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsServiceWANPort, 0, &uHTTPWanPort, sizeof(uHTTPWanPort));
	}

	const char* chSnapLocalPath = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SNAP_LOCAL_PATH);
	if (chSnapLocalPath)
	{
		string snapLocalPath(chSnapLocalPath);
		if (snapLocalPath.back() != '\\')
		{
			snapLocalPath.push_back('\\');
		}
		QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsSnapLocalPath, 0, snapLocalPath.c_str(), snapLocalPath.size());
	}

	const char* chSnapWebPath = parList.DoFindCGIValueForParam(EASY_TAG_CONFIG_SNAP_WEB_PATH);
	if (chSnapWebPath)
	{
		string snapWebPath(chSnapWebPath);
		if (snapWebPath.back() != '\/')
		{
			snapWebPath.push_back('\/');
		}
		QTSS_SetValue(QTSServerInterface::GetServer()->GetPrefs(), qtssPrefsSnapWebPath, 0, snapWebPath.c_str(), snapWebPath.size());
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
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

QTSS_Error HTTPSession::execNetMsgCSRestartReqRESTful(const char* queryString)
{
	/*//��ʱע�͵���ʵ��������Ҫ��֤��
	if(!fAuthenticated)//û�н�����֤����
	return httpUnAuthorized;
	*/

#ifdef WIN32
	::ExitProcess(0);
#else
	exit(0);
#endif //WIN32
}

QTSS_Error HTTPSession::execNetMsgCSGetUsagesReqRESTful(const char* queryString)
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
		value[EASY_TAG_ACTION] = "GetDeviceList";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/getdevicelist";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "GetDeviceInfo";
		value[EASY_TAG_PARAMETER] = "device=[Serial]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/getdeviceinfo?device=00100100001";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "StartDeviceStream";
		value[EASY_TAG_PARAMETER] = "device=[Serial]&channel=[Channel]&reserve=[Reserve]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/startdevicestream?device=001002000001&channel=1&reserve=1";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "StopDeviceStream";
		value[EASY_TAG_PARAMETER] = "device=[Serial]&channel=[Channel]&reserve=[Reserve]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/stopdevicestream?device=001002000001&channel=1&reserve=1";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "PTZControl";
		value[EASY_TAG_PARAMETER] = "device=[Serial]&channel=[Channel]&protocol=[ONVIF/SDK]&actiontype=[Continuous/Single]&command=[Stop/Up/Down/Left/Right/Zoomin/Zoomout/Focusin/Focusout/Aperturein/Apertureout]&speed=[Speed]&reserve=[Reserve]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/ptzcontrol?device=001002000001&channel=1&protocol=onvif&actiontype=single&command=down&speed=5&reserve=1";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_GET;
		value[EASY_TAG_ACTION] = "PresetControl";
		value[EASY_TAG_PARAMETER] = "device=[Serial]&channel=[Channel]&protocol=[ONVIF/SDK]&preset=[Preset]&command=[Goto/Set/Remove]";
		value[EASY_TAG_EXAMPLE] = "http://ip:port/api/v1/presetcontrol?device=001001000058&channel=1&command=goto&preset=1&protocol=onvif";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	{
		Json::Value value;
		value[EASY_TAG_HTTP_METHOD] = EASY_TAG_HTTP_POST;
		value[EASY_TAG_ACTION] = "MSG_CS_TALKBACK_CONTROL_REQ";
		value[EASY_TAG_PARAMETER] = "";
		value[EASY_TAG_EXAMPLE] = "http://ip:port";
		value[EASY_TAG_DESCRIPTION] = "";
		(*proot)[EASY_TAG_ROOT][EASY_TAG_BODY][EASY_TAG_API].append(value);
	}

	rsp.SetHead(header);
	rsp.SetBody(body);

	string msg = rsp.GetMsg();
	this->SendHTTPPacket(msg, false, false);

	return QTSS_NoErr;
}

bool HTTPSession::isRightChannel(const char* channel) const
{
	if (!channel)
	{
		return false;
	}

	try
	{
		int channelNum = boost::lexical_cast<unsigned int>(channel);
		if (channelNum > 64)
		{
			return false;
		}
	}
	catch (...)
	{
		return false;
	}

	return true;
}

/*
	Copyright (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
    File:       EasyCMSSession.cpp
    Contains:   Implementation of object defined in EasyCMSSession.h. 
*/
#include "EasyCMSSession.h"
#include "EasyUtil.h"

// EasyCMS IP
static char*            sEasyCMS_IP		= NULL;
static char*            sDefaultEasyCMS_IP_Addr = "127.0.0.1";
// EasyCMS Port
static UInt16			sEasyCMSPort			= 10000;
static UInt16			sDefaultEasyCMSPort		= 10000;
// EasyCamera Serial
static char*            sEasy_Serial			= NULL;
static char*            sDefaultEasy_Serial		= "CAM00000001";
// EasyCamera Name
static char*            sEasy_Name				= NULL;
static char*            sDefaultEasy_Name		= "CAM001";
// EasyCamera Secret key
static char*            sEasy_Key				= NULL;
static char*            sDefaultEasy_Key		= "123456";
// EasyCamera tag name
static char*			sEasy_Tag				= NULL;
static char*			sDefaultEasy_Tag		= "CAMTag001";
// EasyCMS Keep-Alive Interval
static UInt32			sKeepAliveInterval		= 30;
static UInt32			sDefKeepAliveInterval	= 30;


// ��ʼ����ȡ�����ļ��и�������
void EasyCMSSession::Initialize(QTSS_ModulePrefsObject cmsModulePrefs)
{
	delete [] sEasyCMS_IP;
    sEasyCMS_IP = QTSSModuleUtils::GetStringAttribute(cmsModulePrefs, "easycms_ip", sDefaultEasyCMS_IP_Addr);

	QTSSModuleUtils::GetAttribute(cmsModulePrefs, "easycms_port", qtssAttrDataTypeUInt16, &sEasyCMSPort, &sDefaultEasyCMSPort, sizeof(sEasyCMSPort));

	delete [] sEasy_Serial;
    sEasy_Serial = QTSSModuleUtils::GetStringAttribute(cmsModulePrefs, "device_serial", sDefaultEasy_Serial);
	
	delete [] sEasy_Name;
    sEasy_Name = QTSSModuleUtils::GetStringAttribute(cmsModulePrefs, "device_name", sDefaultEasy_Name);
	
	delete [] sEasy_Key;
    sEasy_Key = QTSSModuleUtils::GetStringAttribute(cmsModulePrefs, "device_key", sDefaultEasy_Key);

	delete[] sEasy_Tag;
	sEasy_Tag = QTSSModuleUtils::GetStringAttribute(cmsModulePrefs, "device_tag", sDefaultEasy_Tag);

	QTSSModuleUtils::GetAttribute(cmsModulePrefs, "keep_alive_interval", qtssAttrDataTypeUInt32, &sKeepAliveInterval, &sDefKeepAliveInterval, sizeof(sKeepAliveInterval));
}

EasyCMSSession::EasyCMSSession()
:	Task(),
	fSocket(NEW TCPClientSocket(Socket::kNonBlockingSocketType)),    
	fTimeoutTask(this, sKeepAliveInterval * 1000),
	fInputStream(fSocket),
	fOutputStream(fSocket, &fTimeoutTask),
	fSessionStatus(kSessionOffline),
	fState(kIdle),
	fRequest(NULL),
	fReadMutex(),
	fMutex(),
	fContentBufferOffset(0),
	fContentBuffer(NULL),
	fSnapReq(NULL)
{
	this->SetTaskName("EasyCMSSession");

	UInt32 inAddr = SocketUtils::ConvertStringToAddr(sEasyCMS_IP);

	if(inAddr)
	{
		fSocket->Set(inAddr, sEasyCMSPort);
	}
	else
	{
		//TODO:���ӱ���Ĭ��EasyCMS������
		;
	}

	fTimeoutTask.RefreshTimeout();

}

EasyCMSSession::~EasyCMSSession()
{
	if(fSocket)
	{
		delete fSocket;
		fSocket = NULL;
	}
	if(fRequest)
	{
		delete fRequest;
		fRequest = NULL;
	}
	if(fContentBuffer)
	{
		delete [] fContentBuffer;
		fContentBuffer = NULL;
	}
}

void EasyCMSSession::CleanupRequest()
{
    if (fRequest != NULL)
    {
        // NULL out any references to the current request
        delete fRequest;
        fRequest = NULL;
    }

	//��շ��ͻ�����
	fOutputStream.ResetBytesWritten();
}

SInt64 EasyCMSSession::Run()
{	
	//OSMutexLocker locker(&fMutex);

	OS_Error theErr = OS_NoErr;
	EventFlags events = this->GetEvents();

	while(1)
	{
		switch (fState)
		{
			case kIdle:
				{
					qtss_printf("kIdle state \n");

					if(!IsConnected())
					{
						// TCPSocketδ���ӵ����,���Ƚ��е�¼����
						DSRegister();
					}
					else
					{
						// TCPSocket�����ӵ�����������־����¼�����
						
						if(events & Task::kStartEvent)
						{
							// �����ӣ���״̬Ϊ����,���½������߶���
							if(kSessionOffline == fSessionStatus)
								DSRegister();
						}

						if(events & Task::kReadEvent)
						{
							// �����ӣ�������Ϣ��Ҫ��ȡ(���ݻ��߶Ͽ�)
							fState = kReadingMessage;
						}

						if(events & Task::kTimeoutEvent)
						{
							// �����ӣ�����ʱ�䵽��Ҫ���ͱ����
							DSRegister();
							fTimeoutTask.RefreshTimeout();
						}

						if(events & Task::kUpdateEvent)
						{
							//�����ӣ����Ϳ��ո���
							DSPostSnap();
						}
					}
					
					// �������Ϣ��Ҫ��������뷢������
					if (fOutputStream.GetBytesWritten() > 0)
					{
						fState = kSendingMessage;
					}

					// 
					if(kIdle == fState)
					{
						return 0;
					}

					break;
				}

			case kReadingMessage:
				{
					qtss_printf("kReadingMessage state \n");
					// ���������Ĵ洢��fInputStream��
					if ((theErr = fInputStream.ReadRequest()) == QTSS_NoErr)
					{
						//���RequestStream����QTSS_NoErr���ͱ�ʾ�Ѿ���ȡ��Ŀǰ���������������
						//���������ܹ���һ�����屨��Header���֣���Ҫ�����ȴ���ȡ...
						fSocket->GetSocket()->SetTask(this);
						fSocket->GetSocket()->RequestEvent(EV_RE);

						fState = kIdle;
						return 0;
					}
                
					if ((theErr != QTSS_RequestArrived) && (theErr != E2BIG) && (theErr != QTSS_BadArgument))
					{
						//Any other error implies that the input connection has gone away.
						// We should only kill the whole session if we aren't doing HTTP.
						// (If we are doing HTTP, the POST connection can go away)
						Assert(theErr > 0);
						// If we've gotten here, this must be an HTTP session with
						// a dead input connection. If that's the case, we should
						// clean up immediately so as to not have an open socket
						// needlessly lingering around, taking up space.
						Assert(!fSocket->GetSocket()->IsConnected());
						this->ResetClientSocket();
						return 0;
					}

					// �������󳬹��˻�����������Bad Request
					if ( (theErr == E2BIG) || (theErr == QTSS_BadArgument) )
					{
						//����HTTP���ģ�������408
						//(void)QTSSModuleUtils::SendErrorResponse(fRequest, qtssClientBadRequest, qtssMsgBadBase64);
						fState = kCleaningUp;
					}
					else
					{
						fState = kProcessingMessage;
					}
					break;
				}
			case kProcessingMessage:
				{
					qtss_printf("kProcessingMessage state \n");

					// �������籨��
					Assert( fInputStream.GetRequestBuffer() );
					Assert(fRequest == NULL);

					// ���ݾ��������Ĺ���HTTPRequest������
					fRequest = NEW HTTPRequest(&QTSServerInterface::GetServerHeader(), fInputStream.GetRequestBuffer());
                
					// ��շ��ͻ�����
					fOutputStream.ResetBytesWritten();

					Assert(theErr == QTSS_RequestArrived);

					// �����յ��ľ��屨��
					ProcessMessage();

					// ÿһ���������Ӧ�����Ƿ�����ɣ������ֱ�ӽ��лظ���Ӧ
					if (fOutputStream.GetBytesWritten() > 0)
					{
						fState = kSendingMessage;
					}
					else
					{
						fState = kCleaningUp;
					}
					break;
				}
			case kSendingMessage:
				{
					qtss_printf("kSendingMessage state \n");

					//������Ӧ����
					theErr = fOutputStream.Flush();
                
					if (theErr == EAGAIN || theErr == EINPROGRESS)
					{
						// If we get this error, we are currently flow-controlled and should
						// wait for the socket to become writeable again
						// ����յ�Socket EAGAIN������ô������Ҫ��Socket�ٴο�д��ʱ���ٵ��÷���
						fSocket->GetSocket()->SetTask(this);
						fSocket->GetSocket()->RequestEvent(fSocket->GetEventMask());
						this->ForceSameThread();
						// We are holding mutexes, so we need to force
						// the same thread to be used for next Run()
						return 0;
					}
					else if (theErr != QTSS_NoErr)
					{
						// Any other error means that the client has disconnected, right?
						Assert(!this->IsConnected());
						ResetClientSocket();
						return 0;
					}
            
					fState = kCleaningUp;
					break;
				}
			case kCleaningUp:
				{
					qtss_printf("kCleaningUp state \n");
					// �����Ѿ��������������Ѿ����͵ı��Ļ���
					// Cleaning up consists of making sure we've read all the incoming Request Body
					// data off of the socket
					////if (this->GetRemainingReqBodyLen() > 0)
					////{
					////	err = this->DumpRequestData();
              
					////	if (err == EAGAIN)
					////	{
					////		fInputSocketP->RequestEvent(EV_RE);
					////		this->ForceSameThread();    // We are holding mutexes, so we need to force
					////									// the same thread to be used for next Run()
					////		return 0;
					////	}
					////}

					// һ������Ķ�ȡ��������Ӧ�����������ȴ���һ�����籨�ģ�
					this->CleanupRequest();
					fState = kIdle;
					
					if(IsConnected())
					{
						fSocket->GetSocket()->SetTask(this);
						fSocket->GetSocket()->RequestEvent(EV_RE | EV_WR);
					}
					return 0;
				}
		}
	}
    return 0;
}

QTSS_Error EasyCMSSession::ProcessMessage()
{
	if(NULL == fRequest) return QTSS_BadArgument;

    //����HTTPRequest����
    QTSS_Error theErr = fRequest->Parse();
    if (theErr != QTSS_NoErr) return QTSS_BadArgument;

	//��ȡ����Content json���ݲ���
	StrPtrLen* lengthPtr = fRequest->GetHeaderValue(httpContentLengthHeader);
	StringParser theContentLenParser(lengthPtr);
    theContentLenParser.ConsumeWhitespace();
    UInt32 content_length = theContentLenParser.ConsumeInteger(NULL);

    if (content_length)
	{	
		qtss_printf("EasyCMSSession::ProcessMessage read content-length:%d \n", content_length);
		// ���content��fContentBuffer��fContentBufferOffset�Ƿ���ֵ����,������ڣ�˵�������Ѿ���ʼ
		// ����content������,���������,������Ҫ��������ʼ��fContentBuffer��fContentBufferOffset
		if (fContentBuffer == NULL)
		{
			fContentBuffer = NEW char[content_length + 1];
			memset(fContentBuffer,0,content_length + 1);
			fContentBufferOffset = 0;
		}
	    
		UInt32 theLen = 0;
		// ��ȡHTTP Content��������
		theErr = fInputStream.Read(fContentBuffer + fContentBufferOffset, content_length - fContentBufferOffset, &theLen);
		Assert(theErr != QTSS_BadArgument);

		if (theErr == QTSS_RequestFailed)
		{
			OSCharArrayDeleter charArrayPathDeleter(fContentBuffer);
			fContentBufferOffset = 0;
			fContentBuffer = NULL;

			return QTSS_RequestFailed;
		}
	    
		qtss_printf("EasyCMSSession::ProcessMessage() Add Len:%d \n", theLen);
		if ((theErr == QTSS_WouldBlock) || (theLen < ( content_length - fContentBufferOffset)))
		{
			//
			// Update our offset in the buffer
			fContentBufferOffset += theLen;
	       
			Assert(theErr == QTSS_NoErr);
			return QTSS_WouldBlock;
		}

		Assert(theErr == QTSS_NoErr);

	    // ������ɱ��ĺ���Զ�����Delete����
		OSCharArrayDeleter charArrayPathDeleter(fContentBuffer);

		qtss_printf("EasyCMSSession::ProcessMessage() Get Complete Msg:\n%s", fContentBuffer);

		EasyProtocol protocol(fContentBuffer);
		int nNetMsg = protocol.GetMessageType();
		switch (nNetMsg)
		{
		case  MSG_SD_REGISTER_ACK:
			{	
				EasyMsgSDRegisterACK ack(fContentBuffer);

				qtss_printf("session id = %s\n", ack.GetBodyValue("SessionID").c_str());
				qtss_printf("device serial = %s\n", ack.GetBodyValue("Serial").c_str());
			}
			break;
		case MSG_SD_PUSH_STREAM_REQ:
			{
				EasyMsgSDPushStreamREQ	startStreamReq(fContentBuffer);
				qtss_printf("Serial = %s\n", startStreamReq.GetBodyValue("Serial").c_str());
				qtss_printf("Server_IP = %s\n", startStreamReq.GetBodyValue("Server_IP").c_str());
				qtss_printf("Server_Port = %s\n", startStreamReq.GetBodyValue("Server_Port").c_str());

				QTSS_RoleParams params;

				string ip = startStreamReq.GetBodyValue("Server_IP");
				params.startStreaParams.inIP = ip.c_str();
				string port = startStreamReq.GetBodyValue("Server_PORT");
				params.startStreaParams.inPort = atoi(port.c_str());
				string serial = startStreamReq.GetBodyValue("Serial");
				params.startStreaParams.inSerial = serial.c_str();
				string protocol = startStreamReq.GetBodyValue("Protocol");
				params.startStreaParams.inProtocol = protocol.c_str();

				string channel = startStreamReq.GetBodyValue("Channel");
				params.startStreaParams.inChannel = channel.c_str();

				string streamID = startStreamReq.GetBodyValue("StreamID");
				params.startStreaParams.inStreamID = streamID.c_str();

				QTSS_Error	errCode = QTSS_NoErr;
				UInt32 fCurrentModule=0;
				UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kStartStreamRole);
				for (; fCurrentModule < numModules; fCurrentModule++)
				{
					QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kStartStreamRole, fCurrentModule);
					errCode = theModule->CallDispatch(Easy_StartStream_Role, &params);
				}
				fCurrentModule = 0;

				EasyJsonValue body;
				body["Serial"] = params.startStreaParams.inSerial;
				body["Protocol"] = params.startStreaParams.inProtocol;
				body["Server_IP"] = params.startStreaParams.inIP;
				body["Server_Port"] = params.startStreaParams.inPort;

				EasyMsgDSPushSteamACK rsp(body, 1, errCode == QTSS_NoErr ? 200 : 404);

				string msg = rsp.GetMsg();
				StrPtrLen jsonContent((char*)msg.data());
				HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);

				if(httpAck.CreateResponseHeader())
				{
					if (jsonContent.Len)
						httpAck.AppendContentLengthHeader(jsonContent.Len);

					//Push msg to OutputBuffer
					char respHeader[2048] = { 0 };
					StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
					strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);
		
					fOutputStream.Put(respHeader);
					if (jsonContent.Len > 0) 
						fOutputStream.Put(jsonContent.Ptr, jsonContent.Len);
				}
			}
			break;
		case MSG_SD_STREAM_STOP_REQ:
			{
				EasyMsgSDStopStreamREQ	stopStreamReq(fContentBuffer);

				QTSS_RoleParams params;

				string serial = stopStreamReq.GetBodyValue("Serial");
				params.stopStreamParams.inSerial = serial.c_str();
				string protocol = stopStreamReq.GetBodyValue("Protocol");
				params.stopStreamParams.inProtocol = protocol.c_str();
				string channel = stopStreamReq.GetBodyValue("Channel");
				params.stopStreamParams.inChannel = channel.c_str();


				QTSS_Error	errCode = QTSS_NoErr;
				UInt32 fCurrentModule=0;
				UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kStopStreamRole);
				for (; fCurrentModule < numModules; fCurrentModule++)
				{
					QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kStopStreamRole, fCurrentModule);
					errCode = theModule->CallDispatch(Easy_StopStream_Role, &params);
				}
				fCurrentModule = 0;

				EasyJsonValue body;
				body["Serial"] = params.stopStreamParams.inSerial;
				body["Channel"] = params.stopStreamParams.inChannel;
				body["Protocol"] = params.stopStreamParams.inProtocol;

				EasyMsgDSStopStreamACK rsp(body, 1, errCode == QTSS_NoErr ? 200 : 404);
				string msg = rsp.GetMsg();

				//��Ӧ
				StrPtrLen jsonContent((char*)msg.data());
				HTTPRequest httpAck(&QTSServerInterface::GetServerHeader(), httpResponseType);

				if(httpAck.CreateResponseHeader())
				{
					if (jsonContent.Len)
						httpAck.AppendContentLengthHeader(jsonContent.Len);

					//Push msg to OutputBuffer
					char respHeader[2048] = { 0 };
					StrPtrLen* ackPtr = httpAck.GetCompleteHTTPHeader();
					strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);
		
					fOutputStream.Put(respHeader);
					if (jsonContent.Len > 0) 
						fOutputStream.Put(jsonContent.Ptr, jsonContent.Len);
				}
			}
			break;
		default:
			break;
		}
	}
	
	// ����fContentBuffer��fContentBufferOffsetֵ
	fContentBufferOffset = 0;
	fContentBuffer = NULL;

	return QTSS_NoErr;
}

//
// ���ԭ��ClientSocket,NEW���µ�ClientSocket
// ��ֵSocket���ӵķ�������ַ�Ͷ˿�
// ����fInputSocket��fOutputSocket��fSocket����
// ����״̬��״̬
void EasyCMSSession::ResetClientSocket()
{
	qtss_printf("EasyCMSSession::ResetClientSocket()\n");

	CleanupRequest();

	fSocket->GetSocket()->Cleanup();
	if(fSocket) delete fSocket;

	fSocket = NEW TCPClientSocket(Socket::kNonBlockingSocketType);
	UInt32 inAddr = SocketUtils::ConvertStringToAddr(sEasyCMS_IP);
	if(inAddr) fSocket->Set(inAddr, sEasyCMSPort);

	fInputStream.AttachToSocket(fSocket);
	fOutputStream.AttachToSocket(fSocket);
	fState = kIdle;
}


QTSS_Error EasyCMSSession::DSRegister()
{
	EasyDevices channels;

	EasyNVR nvr(sEasy_Serial, sEasy_Name, sEasy_Key, sEasy_Tag, channels);

	EasyMsgDSRegisterREQ req(EASY_TERMINAL_TYPE_ARM, EASY_APP_TYPE_CAMERA, nvr);
	string msg = req.GetMsg();

	StrPtrLen jsonContent((char*)msg.data());

	// ����HTTPע�ᱨ��,�ύ��fOutputStream���з���
	HTTPRequest httpReq(&QTSServerInterface::GetServerHeader(), httpRequestType);

	if(!httpReq.CreateRequestHeader()) return QTSS_Unimplemented;

	if (jsonContent.Len)
		httpReq.AppendContentLengthHeader(jsonContent.Len);

	char respHeader[2048] = { 0 };
	StrPtrLen* ackPtr = httpReq.GetCompleteHTTPHeader();
	strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);
		
	fOutputStream.Put(respHeader);
	if (jsonContent.Len > 0) 
		fOutputStream.Put(jsonContent.Ptr, jsonContent.Len);

	return QTSS_NoErr;
}

QTSS_Error EasyCMSSession::UpdateSnapCache(unsigned char *snapPtr, int snapLen, EasyDarwinSnapType snapType)
{
	if(fSnapReq == NULL)
	{
		char szTime[32] = {0,};
		EasyJsonValue body;
		body["Serial"] = sEasy_Serial;

		string type = EasyProtocol::GetSnapTypeString(snapType);

		body["Type"] = type.c_str();
		body["Time"] = szTime;	
		body["Image"] = EasyUtil::Base64Encode((const char*)snapPtr, snapLen);
		
		fSnapReq = new EasyMsgDSPostSnapREQ(body,1);
	}

	this->Signal(Task::kUpdateEvent);
	return QTSS_NoErr;
}

QTSS_Error EasyCMSSession::DSPostSnap()
{
	if(fSnapReq)
	{
		string msg = fSnapReq->GetMsg();

		//�����ϴ�����
		StrPtrLen jsonContent((char*)msg.data());
		HTTPRequest httpReq(&QTSServerInterface::GetServerHeader(), httpRequestType);

		if(httpReq.CreateRequestHeader())
		{
			if (jsonContent.Len)
				httpReq.AppendContentLengthHeader(jsonContent.Len);

			//Push msg to OutputBuffer
			char respHeader[2048] = { 0 };
			StrPtrLen* ackPtr = httpReq.GetCompleteHTTPHeader();
			strncpy(respHeader,ackPtr->Ptr, ackPtr->Len);
			
			fOutputStream.Put(respHeader);
			if (jsonContent.Len > 0) 
				fOutputStream.Put(jsonContent.Ptr, jsonContent.Len);
		}

		delete fSnapReq;
		fSnapReq = NULL;
	}

	return QTSS_NoErr;
}
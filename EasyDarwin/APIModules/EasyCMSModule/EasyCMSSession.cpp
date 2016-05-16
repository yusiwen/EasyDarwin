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

EasyCMSSession::EasyCMSSession()
:	Task(),
	fSocket(NEW TCPClientSocket(Socket::kNonBlockingSocketType)),    
	fTimeoutTask(this, 30 * 1000),
	fInputStream(fSocket),
	fOutputStream(fSocket, &fTimeoutTask),
	fState(kIdle),//״̬���ĳ�ʼ״̬
	fRequest(NULL),
	fReadMutex(),
	fMutex(),
	fContentBufferOffset(0),
	fContentBuffer(NULL),
	fSerial(NULL),
	fChannel(NULL),
	fLiveSession(true)
{
	this->SetTaskName("EasyCMSSession");

	char *chCMSIP = "127.0.0.1";//test,ʵ����CMS��IP�Ͷ˿�Ӧ�ô�redis��ȡ
	UInt16 uCMSPort = 10000;

	UInt32 inAddr = SocketUtils::ConvertStringToAddr(chCMSIP);

	if(inAddr)
	{
		fSocket->Set(inAddr, uCMSPort);
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
	qtss_printf("~EasyCMSSession\n");
	delete[] fSerial;
	delete[] fChannel;

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

	while(fLiveSession)
	{
		switch (fState)
		{
			case kIdle:
				{
					qtss_printf("kIdle state \n");
					//�����¼�����ִ�в�ͬ�Ķ���
					if(events & Task::kStartEvent)
					{
						switch(fMsg)
						{
						case kStreamStopMsg:
							CSFreeStream();
							break;
						default:
							break;
						}
					}

					if(events & Task::kReadEvent)
					{
						// �����ӣ�������Ϣ��Ҫ��ȡ(���ݻ��߶Ͽ�)
						fState = kReadingMessage;
					}

					if(events & Task::kTimeoutEvent)
					{
						return -1;//��ʱ�������������ʾEasyCMS��û�н�������Ļظ�
					}

					if(events & Task::kUpdateEvent)
					{

					}

					// �������Ϣ��Ҫ��������뷢������
					if (fOutputStream.GetBytesWritten() > 0)
					{
						fState = kSendingMessage;
					}

					//��ǰ״̬û�н������ݷ��ͣ���ȴ���һ���¼�����
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
						//this->ResetClientSocket();
						//return 0;
						//��ȡ����ʧ�ܣ�ֱ�ӽ�������
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
						//���������������ʧ�ܣ�ֱ�ӽ�������
						//ResetClientSocket();
						return -1;
					}
            
					fState = kCleaningUp;
					break;
				}
			case kCleaningUp:
				{
					qtss_printf("kCleaningUp state \n");
					// һ������Ķ�ȡ��������Ӧ�����������ȴ���һ�����籨�ģ�
					this->CleanupRequest();
					fState = kIdle;
					if(IsConnected())
					{
						fSocket->GetSocket()->SetTask(this);
						fSocket->GetSocket()->RequestEvent(EV_RE | EV_WR);//�����¼�����
					}
					return 0;
				}
		}
	}
    return -1;
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
		case  MSG_SC_FREE_STREAM_ACK:
			{
				string strErrorNum		=	protocol.GetHeaderValue(EASY_TAG_ERROR_NUM);
				string strSerial		=	protocol.GetHeaderValue(EASY_TAG_SERIAL);
				string strChannle		=	protocol.GetHeaderValue(EASY_TAG_CHANNEL);
				
				qtss_printf("EasyCMSֹͣ������Ӧ:%s,Serial=%s,Channel=%s",strErrorNum.c_str(),strSerial.c_str(),strChannle.c_str());
				fLiveSession=false;//��������
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

QTSS_Error EasyCMSSession::CSFreeStream()
{
	EasyDarwin::Protocol::EasyProtocolACK req(MSG_CS_FREE_STREAM_REQ);
	EasyJsonValue header,body;

	header[EASY_TAG_CSEQ]		=	1;
	header[EASY_TAG_VERSION]	=	EASY_PROTOCOL_VERSION;
	header[EASY_TAG_APP_TYPE]	=	EasyProtocol::GetAppTypeString(EASY_APP_TYPE_EASYDARWIN);

#ifdef __Win32__
	header[EASY_TAG_TERMINAL_TYPE]	=	EasyProtocol::GetAppTypeString(EASY_TERMINAL_TYPE_WINDOWS);
#elif defined __linux__
	header[EASY_TAG_TERMINAL_TYPE]	=	EasyProtocol::GetAppTypeString(EASY_TERMINAL_TYPE_LINUX);
#elif defined __MacOSX__
	header[EASY_TAG_TERMINAL_TYPE]	=	EasyProtocol::GetAppTypeString(EASY_TERMINAL_TYPE_IOS);
#endif


	body[EASY_TAG_SERIAL]		=	fSerial;
	body[EASY_TAG_CHANNEL]		=	fChannel;
	body[EASY_TAG_PROTOCOL]		=	EasyProtocol::GetProtocolString(EASY_PROTOCOL_TYPE_RTSP);
	body[EASY_TAG_RESERVE]		=	"1";

	req.SetHead(header);
	req.SetBody(body);

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

void EasyCMSSession::SetStreamStopInfo(const char * chSerial,const char * chChannel)
{
	fSerial = new char[strlen(chSerial) + 1];
	strcpy(fSerial,chSerial);

	fChannel = new char[strlen(chChannel) + 1];
	strcpy(fChannel,chChannel);

}

void EasyCMSSession::SetMsgType(fEnumMsg msg)
{
	fMsg=msg;
}
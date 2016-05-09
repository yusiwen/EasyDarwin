/*
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Copyright (c) 1999-2008 Apple Inc.  All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 *
 */
/*
	Copyleft (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
    File:       HTTPSessionInterface.cpp
    Contains:   Implementation of HTTPSessionInterface object.
*/

#include "atomic.h"
#include "HTTPSessionInterface.h"
#include "QTSServerInterface.h"
#include "OSMemory.h"
#include <errno.h>
#include "EasyUtil.h"



#if DEBUG
	#define RTSP_SESSION_INTERFACE_DEBUGGING 1
#else
    #define RTSP_SESSION_INTERFACE_DEBUGGING 0
#endif

unsigned int            HTTPSessionInterface::sSessionIndexCounter = kFirstCMSSessionID;

QTSSAttrInfoDict::AttrInfo  HTTPSessionInterface::sAttributes[] = 
{   /*fields:   fAttrName, fFuncPtr, fAttrDataType, fAttrPermission */
    /* 0 */ { "qtssEasySesIndex",            NULL,          qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 1 */ { "qtssRTSPSesLocalAddr",       SetupParams,    qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 2 */ { "qtssRTSPSesLocalAddrStr",    SetupParams,    qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 3 */ { "qtssRTSPSesLocalDNS",        SetupParams,    qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 4 */ { "qtssRTSPSesRemoteAddr",      SetupParams,    qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 5 */ { "qtssRTSPSesRemoteAddrStr",   SetupParams,    qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 6 */ { "qtssRTSPSesEventCntxt",      NULL,           qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 7 */ { "qtssRTSPSesType",            NULL,           qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    /* 8 */ { "qtssEasySesSerial",			NULL,           qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
    
    /* 9 */ { "qtssRTSPSesLastUserName",    NULL,           qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe  },
    /* 10 */{ "qtssRTSPSesLastUserPassword",NULL,           qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe  },
    /* 11 */{ "qtssEasySessionID",			NULL,			qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe  },
    
    /* 12 */{ "qtssRTSPSesLocalPort",       SetupParams,    qtssAttrDataTypeUInt16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    /* 13 */{ "qtssRTSPSesRemotePort",      SetupParams,    qtssAttrDataTypeUInt16,     qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeCacheable },
    
    /* 14 */{ "qtssEasySesLastSMSSessionID",NULL,			qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
	/* 15 */{ "qtssEasySesContentBody",		NULL,           qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite },
	/* 16 */{ "qtssEasySesContentBodyOffset",NULL,          qtssAttrDataTypeUInt32,  qtssAttrModeRead | qtssAttrModePreempSafe | qtssAttrModeWrite }
};


void    HTTPSessionInterface::Initialize()
{
    for (UInt32 x = 0; x < qtssRTSPSesNumParams; x++)
        QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPSessionDictIndex)->
            SetAttribute(x, sAttributes[x].fAttrName, sAttributes[x].fFuncPtr, sAttributes[x].fAttrDataType, sAttributes[x].fAttrPermission);
	
}

HTTPSessionInterface::HTTPSessionInterface()
	: QTSSDictionary(QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kRTSPSessionDictIndex)),
	Task(),
	fTimeoutTask(NULL, QTSServerInterface::GetServer()->GetPrefs()->GetSessionTimeoutInSecs() * 1000),
	fInputStream(&fSocket),
	fOutputStream(&fSocket, &fTimeoutTask),
	fSessionMutex(),
	fSocket(NULL, Socket::kNonBlockingSocketType),
	fOutputSocketP(&fSocket),
	fInputSocketP(&fSocket),
	fSessionType(qtssNormalSession),
	fLiveSession(true),
	fObjectHolders(0),
	fRequestBodyLen(-1),
	fAuthenticated(false),
	fRequestBody(NULL),//add
	fCSeq(1)//add
{
	fStreamReqCount.clear();
    fTimeoutTask.SetTask(this);
    fSocket.SetTask(this);

	//fSerial[0] = 0;
	//::memset(fSerial, 0, EASY_MAX_SERIAL_LENGTH);
	//fDevSerialPtr.Set( fSerial, ::strlen(fSerial));
	//fDevRef.Set( fDevSerialPtr, this);

    fSessionIndex = (UInt32)atomic_add(&sSessionIndexCounter, 1);
    this->SetVal(qtssEasySesIndex, &fSessionIndex, sizeof(fSessionIndex));

    this->SetVal(qtssRTSPSesEventCntxt, &fOutputSocketP, sizeof(fOutputSocketP));
    this->SetVal(qtssRTSPSesType, &fSessionType, sizeof(fSessionType));
	//this->SetEmptyVal(qtssEasySesSerial, &fSerial[0], EASY_MAX_SERIAL_LENGTH);

    this->SetEmptyVal(qtssRTSPSesLastUserName, &fUserNameBuf[0], kMaxUserNameLen);
    this->SetEmptyVal(qtssRTSPSesLastUserPassword, &fUserPasswordBuf[0], kMaxUserPasswordLen);

	qtss_sprintf(fSessionID, "%s", EasyUtil::GetUUID().c_str());
	this->SetValue(qtssEasySessionID, 0, fSessionID, ::strlen(fSessionID), QTSSDictionary::kDontObeyReadOnly);

	this->SetEmptyVal(qtssEasySesLastSMSSessionID, &fLastSMSSessionID[0], QTSS_MAX_SESSION_ID_LENGTH);
    
    fInputStream.ShowMSG(QTSServerInterface::GetServer()->GetPrefs()->GetMSGDebugPrintfs());
    fOutputStream.ShowMSG(QTSServerInterface::GetServer()->GetPrefs()->GetMSGDebugPrintfs());
	//add
	fInfo.cWaitingState=0;//��ʼΪ�����һ������״̬
	fInfo.uWaitingTime=0;//��ʼΪ���õȴ���Ӧ
	/*
	HTTPSessionInterface * p=this;
	int iSize=sizeof(QTSSDictionary)+sizeof(Task)+sizeof(fUserNameBuf)+sizeof(fUserPasswordBuf)+sizeof(fSessionID)+sizeof(fLastSMSSessionID)
		+sizeof(fDevSerial)+sizeof(fStreamReqCount)+sizeof(fNVROperatorMutex)+sizeof(fStreamReqCountMutex)+sizeof(fCond)+sizeof(fNVRMessageQueue)
		+sizeof(fTimeoutTask)+sizeof(fInputStream)+sizeof(fOutputStream)+sizeof(fSessionMutex)+sizeof(fSocket)+sizeof(fOutputSocketP)+sizeof(fInputSocketP)
		+sizeof(fSessionType)+sizeof(fLiveSession)+sizeof(fObjectHolders)+sizeof(fSessionIndex)+sizeof(fLocalAddr)+sizeof(fRemoteAddr)+sizeof(fAuthenticated)
		+sizeof(sSessionIndexCounter)+sizeof(sAttributes)+sizeof(fDevice)+sizeof(fRequestBody)+sizeof(fMutexCSeq)+sizeof(fCSeq)+sizeof(fMsgMap)+sizeof(fin);
	iSize=sizeof(HTTPSessionInterface);*/
}


HTTPSessionInterface::~HTTPSessionInterface()
{
    // If the input socket is != output socket, the input socket was created dynamically
    if (fInputSocketP != fOutputSocketP) 
        delete fInputSocketP;
	
	char remoteAddress[20] = {0};
	StrPtrLen theIPAddressStr(remoteAddress,sizeof(remoteAddress));
	QTSS_GetValue(this, qtssRTSPSesRemoteAddrStr, 0, (void*)theIPAddressStr.Ptr, &theIPAddressStr.Len);
	char msgStr[2048] = { 0 };
	
	//�ͻ������ӶϿ�ʱ�������Զ�ֹͣ���������ŵ���fSessionType�����ж��������
	AutoStopStreamJudge();
	//�ͻ������ӶϿ�ʱ�������Զ�ֹͣ��������
	switch(fSessionType)
	{
	case qtssDeviceSession://�豸
		this->UnRegDevSession();
		this->ReleaseMsgMap();//�ͷ�
		qtss_snprintf(msgStr, sizeof(msgStr), "Device session offline from ip[%s], device_serial[%s]",remoteAddress, fDevice.serial_.c_str());
		break;
	case qtssClientSession://�ͻ���
		qtss_snprintf(msgStr, sizeof(msgStr), "Client session offline from ip[%s]",remoteAddress);
		break;
	case qtssNormalSession://һ��ͻ���
		qtss_snprintf(msgStr, sizeof(msgStr), "Unknown session offline from ip[%s]",remoteAddress);
		break;
	default:
		qtss_snprintf(msgStr, sizeof(msgStr), "Unknown session offline from ip[%s]",remoteAddress);
		break;
	}
	QTSServerInterface::LogError(qtssMessageVerbosity, msgStr);
}

void HTTPSessionInterface::DecrementObjectHolderCount()
{

//#if __Win32__
//maybe don't need this special case but for now on Win32 we do it the old way since the killEvent code hasn't been verified on Windows.
    this->Signal(Task::kReadEvent);//have the object wakeup in case it can go away.
    atomic_sub(&fObjectHolders, 1);
//#else
//    if (0 == atomic_sub(&fObjectHolders, 1))
//        this->Signal(Task::kKillEvent);
//#endif

}

QTSS_Error HTTPSessionInterface::Write(void* inBuffer, UInt32 inLength,
                                            UInt32* outLenWritten, UInt32 inFlags)
{
    UInt32 sendType = HTTPResponseStream::kDontBuffer;
    if ((inFlags & qtssWriteFlagsBufferData) != 0)
        sendType = HTTPResponseStream::kAlwaysBuffer;
    
    iovec theVec[2];
    theVec[1].iov_base = (char*)inBuffer;
    theVec[1].iov_len = inLength;
    return fOutputStream.WriteV(theVec, 2, inLength, outLenWritten, sendType);
}

QTSS_Error HTTPSessionInterface::WriteV(iovec* inVec, UInt32 inNumVectors, UInt32 inTotalLength, UInt32* outLenWritten)
{
    return fOutputStream.WriteV(inVec, inNumVectors, inTotalLength, outLenWritten, HTTPResponseStream::kDontBuffer);
}

QTSS_Error HTTPSessionInterface::Read(void* ioBuffer, UInt32 inLength, UInt32* outLenRead)
{
    //
    // Don't let callers of this function accidently creep past the end of the
    // request body.  If the request body size isn't known, fRequestBodyLen will be -1
    
    if (fRequestBodyLen == 0)
        return QTSS_NoMoreData;
        
    if ((fRequestBodyLen > 0) && ((SInt32)inLength > fRequestBodyLen))
        inLength = fRequestBodyLen;
    
    UInt32 theLenRead = 0;
    QTSS_Error theErr = fInputStream.Read(ioBuffer, inLength, &theLenRead);
    
    if (fRequestBodyLen >= 0)
        fRequestBodyLen -= theLenRead;

    if (outLenRead != NULL)
        *outLenRead = theLenRead;
    
    return theErr;
}

QTSS_Error HTTPSessionInterface::RequestEvent(QTSS_EventType inEventMask)
{
    if (inEventMask & QTSS_ReadableEvent)
        fInputSocketP->RequestEvent(EV_RE);
    if (inEventMask & QTSS_WriteableEvent)
        fOutputSocketP->RequestEvent(EV_WR);
        
    return QTSS_NoErr;
}

/*
    take the TCP socket away from a RTSP session that's
    waiting to be snarfed.
    
*/

void    HTTPSessionInterface::SnarfInputSocket( HTTPSessionInterface* fromRTSPSession )
{
    Assert( fromRTSPSession != NULL );
    Assert( fromRTSPSession->fOutputSocketP != NULL );
    
    fInputStream.SnarfRetreat( fromRTSPSession->fInputStream );

    if (fInputSocketP == fOutputSocketP)
        fInputSocketP = NEW TCPSocket( this, Socket::kNonBlockingSocketType );
    else
        fInputSocketP->Cleanup();   // if this is a socket replacing an old socket, we need
                                    // to make sure the file descriptor gets closed
    fInputSocketP->SnarfSocket( fromRTSPSession->fSocket );
    
    // fInputStream, meet your new input socket
    fInputStream.AttachToSocket( fInputSocketP );
}


void* HTTPSessionInterface::SetupParams(QTSSDictionary* inSession, UInt32* /*outLen*/)
{
    HTTPSessionInterface* theSession = (HTTPSessionInterface*)inSession;
 
    theSession->fLocalAddr = theSession->fSocket.GetLocalAddr();
    theSession->fRemoteAddr = theSession->fSocket.GetRemoteAddr();
    
    theSession->fLocalPort = theSession->fSocket.GetLocalPort();
    theSession->fRemotePort = theSession->fSocket.GetRemotePort();
    
    StrPtrLen* theLocalAddrStr = theSession->fSocket.GetLocalAddrStr();
    StrPtrLen* theLocalDNSStr = theSession->fSocket.GetLocalDNSStr();
    StrPtrLen* theRemoteAddrStr = theSession->fSocket.GetRemoteAddrStr();
    if (theLocalAddrStr == NULL || theLocalDNSStr == NULL || theRemoteAddrStr == NULL)
    {    //the socket is bad most likely values are all 0. If the socket had an error we shouldn't even be here.
         //theLocalDNSStr is set to localAddr if it is unavailable, so it should be present at this point as well.
         Assert(0);   //for debugging
         return NULL; //nothing to set
    }
    theSession->SetVal(qtssRTSPSesLocalAddr, &theSession->fLocalAddr, sizeof(theSession->fLocalAddr));
    theSession->SetVal(qtssRTSPSesLocalAddrStr, theLocalAddrStr->Ptr, theLocalAddrStr->Len);
    theSession->SetVal(qtssRTSPSesLocalDNS, theLocalDNSStr->Ptr, theLocalDNSStr->Len);
    theSession->SetVal(qtssRTSPSesRemoteAddr, &theSession->fRemoteAddr, sizeof(theSession->fRemoteAddr));
    theSession->SetVal(qtssRTSPSesRemoteAddrStr, theRemoteAddrStr->Ptr, theRemoteAddrStr->Len);
    
    theSession->SetVal(qtssRTSPSesLocalPort, &theSession->fLocalPort, sizeof(theSession->fLocalPort));
    theSession->SetVal(qtssRTSPSesRemotePort, &theSession->fRemotePort, sizeof(theSession->fRemotePort));
    return NULL;
}



QTSS_Error HTTPSessionInterface::SendHTTPPacket(StrPtrLen* contentXML, Bool16 connectionClose, Bool16 decrement)
{
	return QTSS_NoErr;
}

QTSS_Error HTTPSessionInterface::RegDevSession(const char* serial, UInt32 serailLen)
{
	//if((::strlen(serial) == 0) || (serailLen == 0))
	//	return QTSS_ValueNotFound;
	//fSessionType = qtssDeviceSession;
	//QTSS_SetValue(this, qtssEasySesSerial, 0, serial, serailLen);

	//fDevSerialPtr.Set( fSerial, serailLen);
	//fDevRef.Set( fDevSerialPtr, this);
	//OS_Error theErr = QTSServerInterface::GetServer()->GetDeviceSessionMap()->Register(GetRef());
	////printf("[line:%d]HTTPSessionInterface::RegDevSession theErr = %d\n",__LINE__, theErr);
	//if(theErr == OS_NoErr)
	//	fAuthenticated = true;
	//return theErr;
	return QTSS_Unimplemented;
}

QTSS_Error HTTPSessionInterface::UpdateDevSnap(const char* inSnapTime, const char* inSnapJpg)
{
	if(!fAuthenticated) return QTSS_NoErr;
	return QTSS_NoErr;
}

void HTTPSessionInterface::UnRegDevSession()
{
	if (fAuthenticated)
	{
		/*
		EasyNVRs &nvrs = QTSServerInterface::GetServer()->GetRegisterNVRs();		
		EasyNVRs::iterator nvr = nvrs.find(fDevSerial);
		if (nvr != nvrs.end())
		{
			nvrs.erase(nvr);
		}
		*/
		QTSServerInterface::GetServer()->GetDeviceMap()->UnRegister(fDevice.serial_);//add
		//��redis��ɾ���豸
		QTSServerInterface::GetServer()->RedisDelDevName(fDevice.serial_.c_str());
	}
}

QTSS_Error HTTPSessionInterface::UpdateDevRedis()
{
	return QTSS_NoErr;
}

UInt32 HTTPSessionInterface::GetStreamReqCount(string camera)
{
	boost::mutex::scoped_lock lock(fStreamReqCountMutex);
	return fStreamReqCount[camera];
}

void HTTPSessionInterface::IncrementStreamReqCount(string camera)
{
	boost::mutex::scoped_lock lock(fStreamReqCountMutex);
	fStreamReqCount[camera]++;
}

void HTTPSessionInterface::DecrementStreamReqCount(string camera)
{
	boost::mutex::scoped_lock lock(fStreamReqCountMutex);
	fStreamReqCount[camera]--;
	if (fStreamReqCount[camera] < 0) fStreamReqCount[camera] = 0;
}
//add
void HTTPSessionInterface::InsertToSet(const string &strCameraSerial,void * pObject)//���뵽set��
{
	OSMutexLocker MutexLocker(&fMutexSet);
	DevMapItera it=fDevmap.find(strCameraSerial);
	if(it==fDevmap.end())//��ʾ���ǶԵ�ǰ�豸�������ͷ�ĵ�һ��ֱ��
	{
		DevSet setTemp;
		setTemp.insert(pObject);
		fDevmap[strCameraSerial]=setTemp;
	}
	else//֮���Ѿ�������
	{
		DevSet *setTemp=&(it->second);//��ȡ��ǰ����ͷֱ���б�
		setTemp->insert(pObject);
	}
}
bool HTTPSessionInterface::EraseInSet(const string &strCameraSerial,void *pObject)//ɾ��Ԫ�أ����ж��Ƿ�Ϊ�գ�Ϊ�շ���true,ʧ�ܷ���false
{
	OSMutexLocker MutexLocker(&fMutexSet);
	DevMapItera it=fDevmap.find(strCameraSerial);
	if(it==fDevmap.end())
	{
		return false;
	}
	else
	{
		DevSet *setTemp=&(it->second);//��ȡ��ǰ����ͷֱ���б�
		setTemp->erase(pObject);
		return setTemp->empty();
	}
}
void HTTPSessionInterface::AutoStopStreamJudge()
{
	OSMutexLocker MutexLocker(&fMutexSet);
	CliStreamMapItera it;
	stStreamInfo stTemp;
	OSRefTableEx* DeviceMap=QTSServerInterface::GetServer()->GetDeviceMap();
	OSRefTableEx::OSRefEx* theDevRef;
	for(it=fClientStreamMap.begin();it!=fClientStreamMap.end();it++)
	{
		stTemp=it->second;
		theDevRef=DeviceMap->Resolve(stTemp.strDeviceSerial);////////////////////////////////++
		if(theDevRef==NULL)
			continue;
		//�ߵ���˵������ָ���豸
		HTTPSessionInterface *pDevSession=(HTTPSessionInterface *)theDevRef->GetObjectPtr();//��õ�ǰ�豸�Ự
		if(pDevSession->EraseInSet(stTemp.strCameraSerial,this))//��ǰ����ͷ�������ͻ���Ϊ�գ������豸����ֹͣ��������
		{
			EasyDarwin::Protocol::EasyDarwinRSP		reqreq(MSG_SD_STREAM_STOP_REQ);
			EasyJsonValue headerheader,bodybody;

			char chTemp[16]={0};
			UInt32 uDevCseq=pDevSession->GetCSeq();
			sprintf(chTemp,"%d",uDevCseq);
			headerheader["CSeq"]	=string(chTemp);//ע������ط�����ֱ�ӽ�UINT32->int,��Ϊ���������ʧ��
			headerheader[EASY_TAG_VERSION]=		EASY_PROTOCOL_VERSION;

			bodybody["DeviceSerial"]	=	stTemp.strDeviceSerial;
			bodybody["CameraSerial"]	=	stTemp.strCameraSerial;
			bodybody["StreamID"]		=   stTemp.strStreamID;
			bodybody["Protocol"]		=	stTemp.strProtocol;

			reqreq.SetHead(headerheader);
			reqreq.SetBody(bodybody);

			string buffer = reqreq.GetMsg();
			QTSS_SendHTTPPacket(pDevSession,(char*)buffer.c_str(),buffer.size(),false,false);
		}
		DeviceMap->Release(stTemp.strDeviceSerial);
	}
}

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
	Copyleft (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*
    File:       QTSServerInterface.cpp
    Contains:   Implementation of object defined in QTSServerInterface.h.
*/

//INCLUDES:

#ifndef kVersionString
#include "revision.h"
#endif
#include "QTSServerInterface.h"
#include "HTTPSessionInterface.h"

#include "OSRef.h"
#include "HTTPProtocol.h"
#ifndef __MacOSX__
#include "revision.h"
#endif

// STATIC DATA

UInt32                  QTSServerInterface::sServerAPIVersion = QTSS_API_VERSION;
QTSServerInterface*     QTSServerInterface::sServer = NULL;
#if __MacOSX__
StrPtrLen               QTSServerInterface::sServerNameStr("EasyCMS");
#else
StrPtrLen               QTSServerInterface::sServerNameStr("EasyCMS");
#endif

// kVersionString from revision.h, include with -i at project level
StrPtrLen               QTSServerInterface::sServerVersionStr(kVersionString);
StrPtrLen               QTSServerInterface::sServerBuildStr(kBuildString);
StrPtrLen               QTSServerInterface::sServerCommentStr(kCommentString);

StrPtrLen               QTSServerInterface::sServerPlatformStr(kPlatformNameString);
StrPtrLen               QTSServerInterface::sServerBuildDateStr(__DATE__ ", "__TIME__);
char                    QTSServerInterface::sServerHeader[kMaxServerHeaderLen];
StrPtrLen               QTSServerInterface::sServerHeaderPtr(sServerHeader, kMaxServerHeaderLen);

QTSSModule**            QTSServerInterface::sModuleArray[QTSSModule::kNumRoles];
UInt32                  QTSServerInterface::sNumModulesInRole[QTSSModule::kNumRoles];
OSQueue                 QTSServerInterface::sModuleQueue;
QTSSErrorLogStream      QTSServerInterface::sErrorLogStream;


QTSSAttrInfoDict::AttrInfo  QTSServerInterface::sConnectedUserAttributes[] = 
{   /*fields:   fAttrName, fFuncPtr, fAttrDataType, fAttrPermission */
    /* 0  */ { "qtssConnectionType",                    NULL,   qtssAttrDataTypeCharArray,      qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 1  */ { "qtssConnectionCreateTimeInMsec",        NULL,   qtssAttrDataTypeTimeVal,        qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 2  */ { "qtssConnectionTimeConnectedInMsec",     TimeConnected,  qtssAttrDataTypeTimeVal,        qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 3  */ { "qtssConnectionBytesSent",               NULL,   qtssAttrDataTypeUInt32,         qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 4  */ { "qtssConnectionMountPoint",              NULL,   qtssAttrDataTypeCharArray,      qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 5  */ { "qtssConnectionHostName",                NULL,   qtssAttrDataTypeCharArray,      qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe } ,

    /* 6  */ { "qtssConnectionSessRemoteAddrStr",       NULL,   qtssAttrDataTypeCharArray,      qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 7  */ { "qtssConnectionSessLocalAddrStr",        NULL,   qtssAttrDataTypeCharArray,      qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    
    /* 8  */ { "qtssConnectionCurrentBitRate",          NULL,   qtssAttrDataTypeUInt32,         qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    /* 9  */ { "qtssConnectionPacketLossPercent",       NULL,   qtssAttrDataTypeFloat32,        qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
    // this last parameter is a workaround for the current dictionary implementation.  For qtssConnectionTimeConnectedInMsec above we have a param
    // retrieval function.  This needs storage to keep the value returned, but if it sets its own param then the function no longer gets called.
    /* 10 */ { "qtssConnectionTimeStorage",             NULL,   qtssAttrDataTypeTimeVal,        qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe },
};

QTSSAttrInfoDict::AttrInfo  QTSServerInterface::sAttributes[] = 
{   /*fields:   fAttrName, fFuncPtr, fAttrDataType, fAttrPermission */
    /* 0  */ { "qtssServerAPIVersion",          NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 1  */ { "qtssSvrDefaultDNSName",         NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    /* 2  */ { "qtssSvrDefaultIPAddr",          NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead },
    /* 3  */ { "qtssSvrServerName",             NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 4  */ { "qtssRTSPSvrServerVersion",      NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 5  */ { "qtssRTSPSvrServerBuildDate",    NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 6  */ { "qtssSvrRTSPPorts",              NULL,   qtssAttrDataTypeUInt16,     qtssAttrModeRead },
    /* 7  */ { "qtssSvrRTSPServerHeader",       NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 8  */ { "qtssSvrState",					NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModeWrite  },
    /* 9  */ { "qtssSvrIsOutOfDescriptors",     IsOutOfDescriptors,     qtssAttrDataTypeBool16, qtssAttrModeRead },
    /* 10 */ { "qtssCurrentSessionCount",		NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead },
	
    /* 11 */ { "qtssSvrHandledMethods",         NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModeWrite | qtssAttrModePreempSafe  },
    /* 12 */ { "qtssSvrModuleObjects",          NULL,   qtssAttrDataTypeQTSS_Object,qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 13 */ { "qtssSvrStartupTime",            NULL,   qtssAttrDataTypeTimeVal,    qtssAttrModeRead },
    /* 14 */ { "qtssSvrGMTOffsetInHrs",         NULL,   qtssAttrDataTypeSInt32,     qtssAttrModeRead },
    /* 15 */ { "qtssSvrDefaultIPAddrStr",       NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead },
    
	/* 16 */ { "qtssSvrPreferences",            NULL,   qtssAttrDataTypeQTSS_Object,qtssAttrModeRead | qtssAttrModeInstanceAttrAllowed},
    /* 17 */ { "qtssSvrMessages",               NULL,   qtssAttrDataTypeQTSS_Object,qtssAttrModeRead },
    /* 18 */ { "qtssSvrClientSessions",         NULL,   qtssAttrDataTypeQTSS_Object,qtssAttrModeRead },
    /* 19 */ { "qtssSvrCurrentTimeMilliseconds",CurrentUnixTimeMilli,   qtssAttrDataTypeTimeVal,qtssAttrModeRead},
    /* 20 */ { "qtssSvrCPULoadPercent",         NULL,   qtssAttrDataTypeFloat32,    qtssAttrModeRead},
    
    /* 21 */ { "qtssSvrConnectedUsers",         NULL, qtssAttrDataTypeQTSS_Object,      qtssAttrModeRead | qtssAttrModeWrite },
    
    /* 22  */ { "qtssSvrServerBuild",           NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 23  */ { "qtssSvrServerPlatform",        NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 24  */ { "qtssSvrRTSPServerComment",     NULL,   qtssAttrDataTypeCharArray,  qtssAttrModeRead | qtssAttrModePreempSafe },
	/* 25  */ { "qtssSvrNumThinned",            NULL,   qtssAttrDataTypeSInt32,     qtssAttrModeRead | qtssAttrModePreempSafe },
    /* 26  */ { "qtssSvrNumThreads",            NULL,   qtssAttrDataTypeUInt32,     qtssAttrModeRead | qtssAttrModePreempSafe }
};

void    QTSServerInterface::Initialize()
{
    for (UInt32 x = 0; x < qtssSvrNumParams; x++)
        QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kServerDictIndex)->
            SetAttribute(x, sAttributes[x].fAttrName, sAttributes[x].fFuncPtr,
                sAttributes[x].fAttrDataType, sAttributes[x].fAttrPermission);

    for (UInt32 y = 0; y < qtssConnectionNumParams; y++)
        QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kQTSSConnectedUserDictIndex)->
            SetAttribute(y, sConnectedUserAttributes[y].fAttrName, sConnectedUserAttributes[y].fFuncPtr,
                sConnectedUserAttributes[y].fAttrDataType, sConnectedUserAttributes[y].fAttrPermission);

    //Write out a premade server header
    StringFormatter serverFormatter(sServerHeaderPtr.Ptr, kMaxServerHeaderLen);
	serverFormatter.Put(HTTPProtocol::GetHeaderString(httpServerHeader)->Ptr, HTTPProtocol::GetHeaderString(httpServerHeader)->Len);
    serverFormatter.Put(": ");
    serverFormatter.Put(sServerNameStr);
    serverFormatter.PutChar('/');
    serverFormatter.Put(sServerVersionStr);
    serverFormatter.PutChar(' ');

    serverFormatter.PutChar('(');
    serverFormatter.Put("Build/");
    serverFormatter.Put(sServerBuildStr);
    serverFormatter.Put("; ");
    serverFormatter.Put("Platform/");
    serverFormatter.Put(sServerPlatformStr);
    serverFormatter.PutChar(';');
 
    if (sServerCommentStr.Len > 0)
    {
        serverFormatter.PutChar(' ');
        serverFormatter.Put(sServerCommentStr);
    }
    serverFormatter.PutChar(')');


    sServerHeaderPtr.Len = serverFormatter.GetCurrentOffset();
    Assert(sServerHeaderPtr.Len < kMaxServerHeaderLen);
}

QTSServerInterface::QTSServerInterface()
 :  QTSSDictionary(QTSSDictionaryMap::GetMap(QTSSDictionaryMap::kServerDictIndex), &fMutex),
    fDeviceSessionMap(NULL),
    fSrvrPrefs(NULL),
    fSrvrMessages(NULL),
    fServerState(qtssStartingUpState),
    fDefaultIPAddr(0),
    fListeners(NULL),
    fNumListeners(0),
    fStartupTime_UnixMilli(0),
    fGMTOffset(0),
    fNumServiceSessions(0),
    fCPUPercent(0),
    fCPUTimeUsedInSec(0),
    fSigInt(false),
    fSigTerm(false),
    fDebugLevel(0),
    fDebugOptions(0),    
    fMaxLate(0),
    fTotalLate(0),
    fCurrentMaxLate(0),
    fTotalQuality(0),
    fNumThinned(0),
    fNumThreads(0),
	fIfConSucess(false)//add
{
    for (UInt32 y = 0; y < QTSSModule::kNumRoles; y++)
    {
        sModuleArray[y] = NULL;
        sNumModulesInRole[y] = 0;
    }

    this->SetVal(qtssSvrState,              &fServerState,              sizeof(fServerState));
    this->SetVal(qtssServerAPIVersion,      &sServerAPIVersion,         sizeof(sServerAPIVersion));
    this->SetVal(qtssSvrDefaultIPAddr,      &fDefaultIPAddr,            sizeof(fDefaultIPAddr));
    this->SetVal(qtssSvrServerName,         sServerNameStr.Ptr,         sServerNameStr.Len);
    this->SetVal(qtssSvrServerVersion,      sServerVersionStr.Ptr,      sServerVersionStr.Len);
    this->SetVal(qtssSvrServerBuildDate,    sServerBuildDateStr.Ptr,    sServerBuildDateStr.Len);
    this->SetVal(qtssSvrRTSPServerHeader,   sServerHeaderPtr.Ptr,       sServerHeaderPtr.Len);
    this->SetVal(qtssCurrentSessionCount,	&fNumServiceSessions,		sizeof(fNumServiceSessions));
    this->SetVal(qtssSvrStartupTime,        &fStartupTime_UnixMilli,    sizeof(fStartupTime_UnixMilli));
    this->SetVal(qtssSvrGMTOffsetInHrs,     &fGMTOffset,                sizeof(fGMTOffset));
    this->SetVal(qtssSvrCPULoadPercent,     &fCPUPercent,               sizeof(fCPUPercent));

    this->SetVal(qtssSvrServerBuild,        sServerBuildStr.Ptr,    sServerBuildStr.Len);
    this->SetVal(qtssSvrRTSPServerComment,  sServerCommentStr.Ptr,  sServerCommentStr.Len);
    this->SetVal(qtssSvrServerPlatform,     sServerPlatformStr.Ptr, sServerPlatformStr.Len);

    this->SetVal(qtssSvrNumThinned,         &fNumThinned,               sizeof(fNumThinned));
    this->SetVal(qtssSvrNumThreads,         &fNumThreads,               sizeof(fNumThreads));

	qtss_sprintf(fDMSServiceID, "EasyCMS%s", "Config EasyCMS Uid");
    
    sServer = this;

}

void QTSServerInterface::LogError(QTSS_ErrorVerbosity inVerbosity, char* inBuffer)
{
    QTSS_RoleParams theParams;
    theParams.errorParams.inVerbosity = inVerbosity;
    theParams.errorParams.inBuffer = inBuffer;

    for (UInt32 x = 0; x < QTSServerInterface::GetNumModulesInRole(QTSSModule::kErrorLogRole); x++)
        (void)QTSServerInterface::GetModule(QTSSModule::kErrorLogRole, x)->CallDispatch(QTSS_ErrorLog_Role, &theParams);

    // If this is a fatal error, set the proper attribute in the RTSPServer dictionary
    if ((inVerbosity == qtssFatalVerbosity) && (sServer != NULL))
    {
        QTSS_ServerState theState = qtssFatalErrorState;
        (void)sServer->SetValue(qtssSvrState, 0, &theState, sizeof(theState));
    }
}

//ֻ�ǽ�DeviceSession�Ƴ�DeviceSessionMap,����ɾ������
void QTSServerInterface::RemoveAllDeviceSession()
{
    OSMutexLocker locker(fDeviceSessionMap->GetMutex());
    for (OSRefHashTableIter theIter(fDeviceSessionMap->GetHashTable()); !theIter.IsDone(); theIter.Next())
    {
		fDeviceSessionMap->TryUnRegister(theIter.GetCurrent());
    }   
}

void QTSServerInterface::SetValueComplete(UInt32 inAttrIndex, QTSSDictionaryMap* inMap,
							UInt32 inValueIndex, void* inNewValue, UInt32 inNewValueLen)
{
    if (inAttrIndex == qtssSvrState)
    {
        Assert(inNewValueLen == sizeof(QTSS_ServerState));
        
        //
        // Invoke the server state change role
        QTSS_RoleParams theParams;
        theParams.stateChangeParams.inNewState = *(QTSS_ServerState*)inNewValue;
        
        static QTSS_ModuleState sStateChangeState = { NULL, 0, NULL, false };
        if (OSThread::GetCurrent() == NULL)
            OSThread::SetMainThreadData(&sStateChangeState);
        else
            OSThread::GetCurrent()->SetThreadData(&sStateChangeState);

        UInt32 numModules = QTSServerInterface::GetNumModulesInRole(QTSSModule::kStateChangeRole);
        {
            for (UInt32 theCurrentModule = 0; theCurrentModule < numModules; theCurrentModule++)
            {  
                QTSSModule* theModule = QTSServerInterface::GetModule(QTSSModule::kStateChangeRole, theCurrentModule);
                (void)theModule->CallDispatch(QTSS_StateChange_Role, &theParams);
            }
        }

        //
        // Make sure to clear out the thread data
        if (OSThread::GetCurrent() == NULL)
            OSThread::SetMainThreadData(NULL);
        else
            OSThread::GetCurrent()->SetThreadData(NULL);
    }
}

void* QTSServerInterface::CurrentUnixTimeMilli(QTSSDictionary* inServer, UInt32* outLen)
{
    QTSServerInterface* theServer = (QTSServerInterface*)inServer;
    theServer->fCurrentTime_UnixMilli = OS::TimeMilli_To_UnixTimeMilli(OS::Milliseconds()); 
    
    // Return the result
    *outLen = sizeof(theServer->fCurrentTime_UnixMilli);
    return &theServer->fCurrentTime_UnixMilli;
}

void* QTSServerInterface::IsOutOfDescriptors(QTSSDictionary* inServer, UInt32* outLen)
{
    QTSServerInterface* theServer = (QTSServerInterface*)inServer;
    
    theServer->fIsOutOfDescriptors = false;
    for (UInt32 x = 0; x < theServer->fNumListeners; x++)
    {
        if (theServer->fListeners[x]->IsOutOfDescriptors())
        {
            theServer->fIsOutOfDescriptors = true;
            break;
        }
    }
    // Return the result
    *outLen = sizeof(theServer->fIsOutOfDescriptors);
    return &theServer->fIsOutOfDescriptors;
}

void* QTSServerInterface::TimeConnected(QTSSDictionary* inConnection, UInt32* outLen)
{
    SInt64 connectTime;
    void* result;
    UInt32 len = sizeof(connectTime);
    inConnection->GetValue(qtssConnectionCreateTimeInMsec, 0, &connectTime, &len);
    SInt64 timeConnected = OS::Milliseconds() - connectTime;
    *outLen = sizeof(timeConnected);
    inConnection->SetValue(qtssConnectionTimeStorage, 0, &timeConnected, sizeof(connectTime));
    inConnection->GetValuePtr(qtssConnectionTimeStorage, 0, &result, outLen);

    // Return the result
    return result;
}


QTSS_Error  QTSSErrorLogStream::Write(void* inBuffer, UInt32 inLen, UInt32* outLenWritten, UInt32 inFlags)
{
    // For the error log stream, the flags are considered to be the verbosity
    // of the error.
    if (inFlags >= qtssIllegalVerbosity)
        inFlags = qtssMessageVerbosity;
        
    QTSServerInterface::LogError(inFlags, (char*)inBuffer);
    if (outLenWritten != NULL)
        *outLenWritten = inLen;
        
    return QTSS_NoErr;
}

void QTSSErrorLogStream::LogAssert(char* inMessage)
{
    QTSServerInterface::LogError(qtssAssertVerbosity, inMessage);
}

//redis,add,begin
bool QTSServerInterface::ConRedis()//����redis������
{
	if(fIfConSucess)
		return true;

	struct timeval timeout = { 0, 500000 }; // 0.5 seconds
	fRedisCon = redisConnectWithTimeout(fRedisIP.c_str(), fRedisPort, timeout);//test,redis��ip�Ͷ˿�Ӧ����xml��ָ��
	if (fRedisCon == NULL || fRedisCon->err)
	{
		if (fRedisCon) {
			qtss_printf("INFO:Connect redis failed,%s\n", fRedisCon->errstr);
			redisFree(fRedisCon);
		} else {
			qtss_printf("INFO:Connect redis failed,can't allocate redis context\n");
		}
		fIfConSucess=false;
	}
	else
	{
		fIfConSucess=true;
		struct timeval timeoutEx = { 1, 0 }; // 1seconds,����socket���պͷ��ͳ�ʱ
		redisSetTimeout(fRedisCon,timeoutEx);

		RedisInit();//���������������ִ�й����У�redis������ʧ���ˣ���������������ʧ�ܻ��߳ɹ�Ҫ��fIfConSucess
		qtss_printf("INFO:Connect redis sucess\n");
	}
	return fIfConSucess;
}
bool QTSServerInterface::RedisCommand(const char * strCommand)//ִ��һЩ�����Ľ�������ֻҪ֪���ɹ�����ʧ�ܣ�һ��Ϊд
{
	OSMutexLocker mutexLock(&fRedisMutex);

	if(!ConRedis())//ÿһ��ִ������֮ǰ��������redis,�����ǰredis��û�гɹ�����
		return false;
	redisReply* reply = (redisReply*)redisCommand(fRedisCon,strCommand);
	//��Ҫע����ǣ�������صĶ�����NULL�����ʾ�ͻ��˺ͷ�����֮��������ش��󣬱����������ӡ�
	if (NULL == reply) 
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}
	//printf("%s\n",strCommand);
	//printf("%s\n",reply->str);
	freeReplyObject(reply);
	return true;
}
bool QTSServerInterface::RedisAddDevName(const char * strPushNmae)
{
	char chTemp[128]={0};//ע��128λ�Ƿ��㹻
	sprintf(chTemp,"sadd %s:%d_DevName %s",fCMSIP.c_str(),fCMSPort,strPushNmae);//���������Ƽ��뵽set��
	return RedisCommand(chTemp);
}
bool QTSServerInterface::RedisDelDevName(const char * strPushNmae)
{
	char chTemp[128]={0};//ע��128λ�Ƿ��㹻
	sprintf(chTemp,"srem %s:%d_DevName %s",fCMSIP.c_str(),fCMSPort,strPushNmae);//���������ƴ�set���Ƴ�
	return RedisCommand(chTemp);
}
bool  QTSServerInterface::RedisTTL()//ע�⵱������һ��ʱ��ܲ�ʱ���ܻ���Ϊ��ʱʱ��ﵽ������key��ɾ������ʱӦ���������ø�key
{

	OSMutexLocker mutexLock(&fRedisMutex);

	bool bReval=false;
	if(!ConRedis())//ÿһ��ִ������֮ǰ��������redis,�����ǰredis��û�гɹ�����
		return false;

	char chTemp[128]={0};//ע��128λ�Ƿ��㹻
	sprintf(chTemp,"expire  %s:%d_Live 15",fCMSIP.c_str(),fCMSPort);//���ĳ�ʱʱ��

	redisReply* reply = (redisReply*)redisCommand(fRedisCon,chTemp);
	//��Ҫע����ǣ�������صĶ�����NULL�����ʾ�ͻ��˺ͷ�����֮��������ش��󣬱����������ӡ�
	if (NULL == reply) 
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}

	if(reply->type==REDIS_REPLY_INTEGER&&reply->integer==1)//�������
	{
		bReval=true;
	}
	else if(reply->type==REDIS_REPLY_INTEGER&&reply->integer==0)//˵����ǰkey�Ѿ���������,��ô������Ҫ�������ɸ�key
	{
		sprintf(chTemp,"setex %s:%d_Live 15 1",fCMSIP.c_str(),fCMSPort);
		bReval=RedisCommand(chTemp);
	}
	else//�������
	{
		//��redis������������д��Ȩ�ޣ��������ļ������˳־û�ʱ�����ܳ����������
		Assert(0);
	}
	freeReplyObject(reply);
	return bReval;
}
//�Թ��ߵķ�ʽִ�д���������ٵȴ�ʱ��,���������һ��������Ҫ�õ���һ������ķ��ؽ�������޷�ʹ�ù��ߡ�
bool QTSServerInterface::RedisGetAssociatedDarWin(string& strDeviceSerial,string &strCameraSerial,string& strDssIP,string& strDssPort)//����뵱ǰ�豸���кź�����ͷ���кŹ�����EasyDarWin,Ҳ��˵�ҵ����豸�������͵�EasyDarWin
{
	//�㷨���������darwin�б�����ÿһ��darwin�ж����Ƿ����������ݲ��������������б��Ƿ����ָ�����豸���кź�����ͷ���к�
	OSMutexLocker mutexLock(&fRedisMutex);
	
	if(!ConRedis())
		return false;

	bool bReVal=false;
	//���豸���кź��������кźϳ��������ƣ�EasyDarWIn��redis�ϵĴ洢Ϊ���豸���к�/����ͷ���к�.sdp
	string strPushName=strDeviceSerial+'/'+strCameraSerial+".sdp";
	char chTemp[128]={0};
	sprintf(chTemp,"smembers EasyDarWinName");

	
	redisReply* reply = (redisReply*)redisCommand(fRedisCon,chTemp);//���EsayDarWin�б�
	if (NULL == reply)//������Ҫ��������
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}
	printf("%d\n",reply->type);
	if(reply->elements>0&&reply->type==REDIS_REPLY_ARRAY)//smembers���ص�������
	{
		//�������ʹ�ù���
		redisReply* childReply=NULL;
		for(size_t i=0;i<reply->elements;i++)//����ÿһ��EasyDarWin�ж������Ժͣ���ָ���豸���кź�����ͷ���кţ�������
		{
			childReply		=	reply->element[i];
			string strChileReply(childReply->str);

			sprintf(chTemp,"exists %s",(strChileReply+"_Live").c_str());//�ж�EasyDarWin�Ƿ���
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
			
			sprintf(chTemp,"sismember %s %s",(strChileReply+"_PushName").c_str(),strPushName.c_str());//�ж�DarWin���Ƿ���ڸ������豸
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
		}

		redisReply *reply2=NULL,*reply3=NULL;
		for(size_t i=0;i<reply->elements;i++)
		{
			if(redisGetReply(fRedisCon,(void**)&reply2)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply2);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
			if(redisGetReply(fRedisCon,(void**)&reply3)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply3);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(reply2->type==REDIS_REPLY_INTEGER&&reply2->integer==1&&
				reply3->type==REDIS_REPLY_INTEGER&&reply3->integer==1)
			{//�ҵ���
				string strIpPort(reply->element[i]->str);
				int ipos=strIpPort.find(':');//�����ж�
				strDssIP=strIpPort.substr(0,ipos);
				strDssPort=strIpPort.substr(ipos+1,strIpPort.size()-ipos-1);
				

				//freeReplyObject(reply2);
				//freeReplyObject(reply3);
				bReVal=true;
				//break;//�ҵ���Ҳ���ܼ�break,������Ҫִ���꣬��Ϊ��ÿһ�����������һ��Ӧ�𣬷������ͷ����˻���
			}
			freeReplyObject(reply2);
			freeReplyObject(reply3);
		}
	}
	else//û�п��õ�EasyDarWin
	{

	}
	freeReplyObject(reply);
	return bReVal;
}
bool QTSServerInterface::RedisGetBestDarWin(string& strDssIP,string& strDssPort)
{
	//�㷨��������ȡDarWin�б�Ȼ���ȡÿһ��darWin�Ĵ����Ϣ��RTP����
	OSMutexLocker mutexLock(&fRedisMutex);

	if(!ConRedis())
		return false;

	bool bReVal=true;
	char chTemp[128]={0};
	sprintf(chTemp,"smembers EasyDarWinName");

	redisReply* reply = (redisReply*)redisCommand(fRedisCon,chTemp);//���EsayDarWin�б�
	if (NULL == reply)//������Ҫ��������
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}

	if(reply->elements>0&&reply->type==REDIS_REPLY_ARRAY)//smembers���ص�������
	{
		//�������ʹ�ù���
		redisReply* childReply=NULL;
		for(size_t i=0;i<reply->elements;i++)//����ÿһ��EasyDarWin�ж������Ժ�RTP����
		{
			childReply		=	reply->element[i];
			string strChileReply(childReply->str);

			sprintf(chTemp,"exists %s",(strChileReply+"_Live").c_str());//�ж�EasyDarWin�Ƿ���
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
			
			sprintf(chTemp,"hget %s %s",(strChileReply+"_Info").c_str(),"RTP");//�ж�DarWin���Ƿ���ڸ������豸
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
		}

		int key=-1,keynum=0;
		redisReply *reply2=NULL,*reply3=NULL;
		for(size_t i=0;i<reply->elements;i++)
		{
			if(redisGetReply(fRedisCon,(void**)&reply2)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply2);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(redisGetReply(fRedisCon,(void**)&reply3)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply3);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(reply2->type==REDIS_REPLY_INTEGER&&reply2->integer==1&&
				reply3->type==REDIS_REPLY_STRING)//���filed��Ӧ��value���ڣ�����REDIS_REPLY_STRING�����򷵻�REDIS_REPLY_NIL
			{//�ҵ���
				int RTPNum=atoi(reply3->str);
				if(key==-1)
				{
					key=i;
					keynum=RTPNum;
				}
				else
				{
					if(RTPNum<keynum)//�ҵ����õ���
					{
						key=i;
						keynum=RTPNum;
					}
				}
			}
			freeReplyObject(reply2);
			freeReplyObject(reply3);
		}
		if(key==-1)//û�д���
		{
			bReVal=false;
		}
		else
		{
			string strIpPort(reply->element[key]->str);
			int ipos	=		strIpPort.find(':');//�����ж�
			strDssIP	=		strIpPort.substr(0,ipos);
			strDssPort	=		strIpPort.substr(ipos+1,strIpPort.size()-ipos-1);
		}
	}
	else//û�п��õ�EasyDarWin
	{
		bReVal=false;
	}
	freeReplyObject(reply);
	return bReVal;
}
bool QTSServerInterface::RedisGetAssociatedRms(string& strDeviceSerial,string &strCameraSerial,string& strRmsIP,string& strRmsPort)//����뵱ǰ�豸���кź�����ͷ���кŹ�����RMS
{
	//�㷨���������RMS�б�����ÿһ��RMS�ж����Ƿ���������������¼���豸�б�
	OSMutexLocker mutexLock(&fRedisMutex);

	if(!ConRedis())
		return false;

	bool bReVal=false;
	string strRecordingDevName=strDeviceSerial+'_'+strCameraSerial;//�豸���к�_����ͷ���к�
	char chTemp[128]={0};
	sprintf(chTemp,"smembers RMSName");

	redisReply* reply = (redisReply*)redisCommand(fRedisCon,chTemp);//���RMS�б�
	if (NULL == reply)//������Ҫ��������
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}

	if(reply->elements>0&&reply->type==REDIS_REPLY_ARRAY)//smembers���ص�������
	{
		//�������ʹ�ù���
		redisReply* childReply=NULL;
		for(size_t i=0;i<reply->elements;i++)//����ÿһ��RMS�ж������Ժͣ�ָ���豸���кź�����ͷ���кţ��Ƿ����
		{
			childReply		=	reply->element[i];
			string strChileReply(childReply->str);

			sprintf(chTemp,"exists %s",(strChileReply+"_Live").c_str());//�ж�RMS�Ƿ���
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			sprintf(chTemp,"sismember %s %s",(strChileReply+"_RecordingDevName").c_str(),strRecordingDevName.c_str());//�ж�RMS�Ƿ�Ը��豸����¼��
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
		}

		redisReply *reply2=NULL,*reply3=NULL;
		for(size_t i=0;i<reply->elements;i++)
		{
			if(redisGetReply(fRedisCon,(void**)&reply2)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply2);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(redisGetReply(fRedisCon,(void**)&reply3)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply3);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(reply2->type==REDIS_REPLY_INTEGER&&reply2->integer==1&&
				reply3->type==REDIS_REPLY_INTEGER&&reply3->integer==1)
			{//�ҵ���
				string strIpPort(reply->element[i]->str);
				int ipos=strIpPort.find(':');//�����ж�
				strRmsIP=strIpPort.substr(0,ipos);
				strRmsPort=strIpPort.substr(ipos+1,strIpPort.size()-ipos-1);


				//freeReplyObject(reply2);
				//freeReplyObject(reply3);
				bReVal=true;
				//break;
			}
			freeReplyObject(reply2);
			freeReplyObject(reply3);
		}
	}
	else//û�п��õ�RMS
	{

	}
	freeReplyObject(reply);
	return bReVal;
}
bool QTSServerInterface::RedisGetBestRms(string& strRmsIP,string& strRmsPort)//��õ�ǰ¼������С��RMS
{
	//�㷨��������ȡRMS�б�Ȼ���ȡÿһ��RMS�Ĵ����Ϣ������¼�����
	OSMutexLocker mutexLock(&fRedisMutex);

	if(!ConRedis())
		return false;

	bool bReVal=true;
	char chTemp[128]={0};
	sprintf(chTemp,"smembers RMSName");

	redisReply* reply = (redisReply*)redisCommand(fRedisCon,chTemp);//���EsayDarWin�б�
	if (NULL == reply)//������Ҫ��������
	{
		redisFree(fRedisCon);
		fIfConSucess=false;
		return false;
	}

	if(reply->elements>0&&reply->type==REDIS_REPLY_ARRAY)//smembers���ص�������
	{
		//�������ʹ�ù���
		redisReply* childReply=NULL;
		for(size_t i=0;i<reply->elements;i++)//����ÿһ��EasyDarWin�ж������Ժ�RTP����
		{
			childReply		=	reply->element[i];
			string strChileReply(childReply->str);

			sprintf(chTemp,"exists %s",(strChileReply+"_Live").c_str());//�ж�RMSn�Ƿ���
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			sprintf(chTemp,"hget %s %s",(strChileReply+"_Info").c_str(),"RecordingCount");//��ȡRMS��ǰ¼�����
			if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			{
				freeReplyObject(reply);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}
		}

		int key=-1,keynum=0;
		redisReply *reply2=NULL,*reply3=NULL;
		for(size_t i=0;i<reply->elements;i++)
		{
			if(redisGetReply(fRedisCon,(void**)&reply2)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply2);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(redisGetReply(fRedisCon,(void**)&reply3)!=REDIS_OK)
			{
				freeReplyObject(reply);
				freeReplyObject(reply3);
				redisFree(fRedisCon);
				fIfConSucess=false;
				return false;
			}

			if(reply2->type==REDIS_REPLY_INTEGER&&reply2->integer==1&&
				reply3->type==REDIS_REPLY_STRING)//���filed��Ӧ��value���ڣ�����REDIS_REPLY_STRING�����򷵻�REDIS_REPLY_NIL
			{//�ҵ���
				int RecordingNum=atoi(reply3->str);
				if(key==-1)
				{
					key=i;
					keynum=RecordingNum;
				}
				else
				{
					if(RecordingNum<keynum)//�ҵ����õ���
					{
						key=i;
						keynum=RecordingNum;
					}
				}
			}
			freeReplyObject(reply2);
			freeReplyObject(reply3);
		}
		if(key==-1)//û�д���
		{
			bReVal=false;
		}
		else
		{
			string strIpPort(reply->element[key]->str);
			int ipos	=		strIpPort.find(':');//�����ж�
			strRmsIP	=		strIpPort.substr(0,ipos);
			strRmsPort	=		strIpPort.substr(ipos+1,strIpPort.size()-ipos-1);
		}
	}
	else//û�п��õ�EasyDarWin
	{
		bReVal=false;
	}
	freeReplyObject(reply);
	return bReVal;
}
bool QTSServerInterface::RedisGenSession(string& strSessioionID,UInt32 iTimeoutMil)//����Ψһ��sessionID���洢��redis��
{
	//�㷨���٣��������sessionID����redis���Ƿ��д洢��û�оʹ���redis�ϣ��еĻ��������ɣ�ֱ��û��Ϊֹ
	OSMutexLocker mutexLocker(&fRedisMutex);
	
	if(!ConRedis())
		return false;

	redisReply* reply=NULL;
	char chTemp[128]={0};
	
	do 
	{
		if(reply)//�ͷ���һ����Ӧ
			freeReplyObject(reply);

		strSessioionID=OSMapEx::GenerateSessionIdForRedis(fCMSIP.c_str(),fCMSPort);//����
		sprintf(chTemp,"exists SessionID_%s",strSessioionID.c_str());
		reply = (redisReply*)redisCommand(fRedisCon,chTemp);
		if (NULL == reply)//������Ҫ��������
		{
			redisFree(fRedisCon);
			fIfConSucess=false;
			return false;
		}
	}
	while(reply->type==REDIS_REPLY_INTEGER&&reply->integer==1);
	freeReplyObject(reply);//�ͷ����һ���Ļ�Ӧ
	
	//�ߵ���˵���ҵ���һ��Ψһ��SessionID�����ڽ����洢��redis��
	sprintf(chTemp,"setex SessionID_%s %d 1",strSessioionID.c_str(),iTimeoutMil/1000);//�߼��汾֧��setpx�����ó�ʱʱ��Ϊms
	return RedisCommand(chTemp);
}
bool QTSServerInterface::RedisInit()//����redis�ɹ�֮����øú���ִ��һЩ��ʼ���Ĺ���
{
	//ÿһ����redis���Ӻ󣬶�Ӧ�������һ�ε����ݴ洢��ʹ�ø��ǻ���ֱ������ķ�ʽ,��������ʹ�ù��߸��Ӹ�Ч
	char chTemp[128]={0};
	char chPassword[]="~ziguangwulian~iguangwulian~guangwulian~uangwulian";
	do 
	{
		//1,redis������֤
		sprintf(chTemp,"auth %s",chPassword);
		if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			break;

		//2,CMSΨһ��Ϣ�洢(������һ�εĴ洢)
		sprintf(chTemp,"sadd CMSName %s:%d",fCMSIP.c_str(),fCMSPort);
		if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			break;

		//3,CMS���Դ洢,���ö��filedʹ��hmset������ʹ��hset(������һ�εĴ洢)
		sprintf(chTemp,"hmset %s:%d_Info IP %s PORT %d",fCMSIP.c_str(),fCMSPort,fCMSIP.c_str(),fCMSPort);
		if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			break;
		//4,����豸���ƴ洢����Ϊ����֮ǰ������֮����豸����һ��÷����˱仯����˱�����ִ���������
		sprintf(chTemp,"del %s:%d_DevName",fCMSIP.c_str(),fCMSPort);
		if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			break;

		OSMutex *mutexMap=fDeviceMap.GetMutex();
		OSHashMap  *deviceMap=fDeviceMap.GetMap();
		OSRefIt itRef;
		string strAllDevices;
		mutexMap->Lock();
		for(itRef=deviceMap->begin();itRef!=deviceMap->end();itRef++)
		{
			strDevice *deviceInfo=(((HTTPSessionInterface*)(itRef->second->GetObjectPtr()))->GetDeviceInfo());
			strAllDevices=strAllDevices+' '+deviceInfo->serial_;
		}
		mutexMap->Unlock();

		char *chNewTemp=new char[strAllDevices.size()+128];//ע�⣬���ﲻ����ʹ��chTemp����Ϊ���Ȳ�ȷ�������ܵ��»��������
		//5,�豸���ƴ洢
		sprintf(chNewTemp,"sadd %s:%d_DevName%s",fCMSIP.c_str(),fCMSPort,strAllDevices.c_str());
		if(redisAppendCommand(fRedisCon,chNewTemp)!=REDIS_OK)
		{
			delete[] chNewTemp;
			break;
		}
		delete[] chNewTemp;

		//6,�������15�룬��֮��ǰCMS�Ѿ���ʼ�ṩ������
		sprintf(chTemp,"setex %s:%d_Live 15 1",fCMSIP.c_str(),fCMSPort);
		if(redisAppendCommand(fRedisCon,chTemp)!=REDIS_OK)
			break;

		bool bBreak=false;
		redisReply* reply = NULL;
		for(int i=0;i<6;i++)
		{
			if(REDIS_OK != redisGetReply(fRedisCon,(void**)&reply))
			{
				bBreak=true;
				freeReplyObject(reply);
				break;
			}
			freeReplyObject(reply);
		}
		if(bBreak)//˵��redisGetReply�����˴���
			break;
		return true;
	} while (0);
	//�ߵ���˵�������˴�����Ҫ��������,������������һ��ִ������ʱ����,����������ñ�־λ
	redisFree(fRedisCon);
	fIfConSucess=false;
	
	return false;
}
//redis.add,end
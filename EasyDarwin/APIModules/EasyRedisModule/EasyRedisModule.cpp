/*
	Copyright (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#include <stdio.h>

#include "EasyRedisModule.h"
#include "OSHeaders.h"
#include "QTSSModuleUtils.h"
#include "QTSServerInterface.h"
#include "ReflectorSession.h"
#include "EasyUtil.h"

#include "Windows/hiredis.h"
#include "Format.h"
#include "EasyRedisClient.h"

#include "Resources.h"

// STATIC VARIABLES
static QTSS_ModulePrefsObject	modulePrefs = nullptr;
static QTSS_PrefsObject			sServerPrefs = nullptr;
static QTSS_ServerObject		sServer = nullptr;

// Redis IP
static char*            sRedis_IP = nullptr;
static char*            sDefaultRedis_IP_Addr = "127.0.0.1";
// Redis Port
static UInt16			sRedisPort = 6379;
static UInt16			sDefaultRedisPort = 6379;
// Redis password
static char*            sRedisPassword = nullptr;
static char*            sDefaultRedisPassword = "admin";

static bool				sIfConSucess = false;
static OSMutex			sMutex;

static redisContext*	redisContext_ = nullptr;

static void RedisErrorHandler(function<void()> func);

// FUNCTION PROTOTYPES
static QTSS_Error   EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock);
static QTSS_Error   Register(QTSS_Register_Params* inParams);
static QTSS_Error   Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error   RereadPrefs();
static QTSS_Error	RedisConnect();
static QTSS_Error	RedisTTL();
static QTSS_Error	RedisUpdateStream(Easy_StreamInfo_Params* inParams);
static QTSS_Error	RedisSetRTSPLoad();
static QTSS_Error	RedisGetAssociatedCMS(QTSS_GetAssociatedCMS_Params* inParams);
static QTSS_Error	RedisJudgeStreamID(QTSS_JudgeStreamID_Params* inParams);

QTSS_Error EasyRedisModule_Main(void* inPrivateArgs)
{
	return _stublibrary_main(inPrivateArgs, EasyRedisModuleDispatch);
}

QTSS_Error EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock)
{
	switch (inRole)
	{
	case QTSS_Register_Role:
		return Register(&inParamBlock->regParams);
	case QTSS_Initialize_Role:
		return Initialize(&inParamBlock->initParams);
	case QTSS_RereadPrefs_Role:
		return RereadPrefs();
	case Easy_RedisTTL_Role:
		return RedisTTL();
	case Easy_RedisSetRTSPLoad_Role:
		return RedisSetRTSPLoad();
	case Easy_RedisUpdateStreamInfo_Role:
		return RedisUpdateStream(&inParamBlock->easyStreamInfoParams);
	case Easy_RedisGetAssociatedCMS_Role:
		return RedisGetAssociatedCMS(&inParamBlock->GetAssociatedCMSParams);
	case Easy_RedisJudgeStreamID_Role:
		return RedisJudgeStreamID(&inParamBlock->JudgeStreamIDParams);
	default: break;
	}
	return QTSS_NoErr;
}

QTSS_Error Register(QTSS_Register_Params* inParams)
{
	// Do role setup
	(void)QTSS_AddRole(QTSS_Initialize_Role);
	(void)QTSS_AddRole(QTSS_RereadPrefs_Role);
	(void)QTSS_AddRole(Easy_RedisTTL_Role);
	(void)QTSS_AddRole(Easy_RedisSetRTSPLoad_Role);
	(void)QTSS_AddRole(Easy_RedisUpdateStreamInfo_Role);
	(void)QTSS_AddRole(Easy_RedisGetAssociatedCMS_Role);
	(void)QTSS_AddRole(Easy_RedisJudgeStreamID_Role);

	// Tell the server our name!
	static char* sModuleName = "EasyRedisModule";
	::strcpy(inParams->outModuleName, sModuleName);

	return QTSS_NoErr;
}

QTSS_Error Initialize(QTSS_Initialize_Params* inParams)
{
	QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);
	sServer = inParams->inServer;
	sServerPrefs = inParams->inPrefs;
	modulePrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);

	RereadPrefs();

	RedisConnect();

	return QTSS_NoErr;
}

QTSS_Error RereadPrefs()
{
	delete[] sRedis_IP;
	sRedis_IP = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_ip", sDefaultRedis_IP_Addr);

	QTSSModuleUtils::GetAttribute(modulePrefs, "redis_port", qtssAttrDataTypeUInt16, &sRedisPort, &sDefaultRedisPort, sizeof(sRedisPort));

	delete[] sRedisPassword;
	sRedisPassword = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_password", sDefaultRedisPassword);

	return QTSS_NoErr;
}

QTSS_Error RedisConnect()
{
	if (sIfConSucess)
	{
		return QTSS_NoErr;
	}

	struct timeval timeout = { 1, 500000 }; //1.5 seconds
	redisContext_ = redisConnectWithTimeout(sRedis_IP, sRedisPort, timeout);
	if (!redisContext_ || redisContext_->err)
	{
		if (redisContext_)
		{
			RedisErrorHandler([&]() {});
		}
		else
		{
			printf("Connection error: can't allocate redis context\n");
		}

		return QTSS_NotConnected;
	}

	sIfConSucess = true;

	char chKey[128] = { 0 };
	sprintf(chKey, "auth %s", sRedisPassword);

	auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
	if (!reply || string(reply->str) != string("OK"))
	{
		RedisErrorHandler([&]()
		{
			if (reply)
			{
				freeReplyObject(reply);
			}
		});

		return QTSS_NotConnected;
	}
	freeReplyObject(reply);

	return QTSS_NoErr;
}

QTSS_Error RedisTTL()
{
	OSMutexLocker mutexLock(&sMutex);

	if (RedisConnect() != QTSS_NoErr)
	{
		return QTSS_NotConnected;
	}
	auto server = QTSServerInterface::GetServerName().Ptr;
	auto guid = QTSServerInterface::GetServer()->GetCloudServiceNodeID();
	auto load = QTSServerInterface::GetServer()->GetNumRTPSessions();

	char chKey[128] = { 0 };
	sprintf(chKey, "expire %s:%s 15", server, guid);
	auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));

	if (!reply)
	{
		RedisErrorHandler([&]() {});

		return QTSS_NotConnected;
	}

	if (reply->integer == 0)
	{
		auto ip = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP();
		auto http = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWanPort();
		auto rtsp = QTSServerInterface::GetServer()->GetPrefs()->GetRTSPWANPort();

		sprintf(chKey, "hmset %s:%s %s %s %s %d %s %d %s %d", server, guid, EASY_DARWIN_REDIS_IP, ip, EASY_DARWIN_REDIS_HTTP,
			http, EASY_DARWIN_REDIS_RTSP, rtsp, EASY_DARWIN_REDIS_LOAD, load);
		auto replyHmset = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
		if (!replyHmset)
		{
			RedisErrorHandler([&]()
			{
				freeReplyObject(reply);
			});

			return QTSS_NotConnected;
		}
		freeReplyObject(replyHmset);

		sprintf(chKey, "expire %s:%s 15", server, guid);
		auto replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
		if (!replyExpire)
		{
			RedisErrorHandler([&]()
			{
				freeReplyObject(reply);
			});

			return QTSS_NotConnected;
		}
		freeReplyObject(replyExpire);
	}
	else if (reply->integer == 1)
	{
		//TODO::nothing
	}
	freeReplyObject(reply);

	return QTSS_NoErr;
}

QTSS_Error RedisUpdateStream(Easy_StreamInfo_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);
	if (!sIfConSucess)
		return QTSS_NotConnected;

	char chKey[128] = { 0 };
	sprintf(chKey, "%s:%s/%d", EASY_DARWIN_REDIS_LIVE, inParams->inStreamName, inParams->inChannel);

	if (inParams->inAction == easyRedisActionDelete)
	{
		sprintf(chKey, "hdel %s", chKey);
		auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
		if (!reply)
		{
			RedisErrorHandler([&]() {});

			return QTSS_NotConnected;
		}
		return QTSS_NoErr;
	}

	sprintf(chKey, "hmset %s:%s/%d %s %d %s %d %s %s", EASY_DARWIN_REDIS_LIVE, inParams->inStreamName, inParams->inChannel,
		EASY_DARWIN_REDIS_BITRATE, inParams->inBitrate, EASY_DARWIN_REDIS_OUTPUT, inParams->inNumOutputs,
		EASY_DARWIN_REDIS_EASYDARWIN, QTSServerInterface::GetServer()->GetCloudServiceNodeID());
	auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
	if (!reply)
	{
		RedisErrorHandler([&]() {});

		return QTSS_NotConnected;
	}

	if (string(reply->str) == string("OK"))
	{
		sprintf(chKey, "expire %s:%s/%d 150", EASY_DARWIN_REDIS_LIVE, inParams->inStreamName, inParams->inChannel);
		auto replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
		if (!replyExpire)
		{
			RedisErrorHandler([&]()
			{
				freeReplyObject(reply);
			});

			return QTSS_NotConnected;
		}
		freeReplyObject(replyExpire);
	}
	else
	{
		freeReplyObject(reply);
		return QTSS_RequestFailed;
	}

	freeReplyObject(reply);

	return QTSS_NoErr;
}

QTSS_Error RedisGetAssociatedCMS(QTSS_GetAssociatedCMS_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	if (!sIfConSucess)
	{
		return QTSS_NotConnected;
	}

	string exists = Format("exists %s:%s", string(EASY_DARWIN_REDIS_DEVICE), string(inParams->inSerial));
	auto reply = static_cast<redisReply*>(redisCommand(redisContext_, exists.c_str()));
	if (!reply)
	{
		RedisErrorHandler([&]() {});

		return QTSS_NotConnected;
	}

	if (reply->integer == 1)
	{
		string strTemp = Format("hmget %s:%s %s", string(EASY_DARWIN_REDIS_DEVICE), string(inParams->inSerial),
			string(EASY_DARWIN_REDIS_EASYCMS));
		auto replyHmget = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
		if (!replyHmget)
		{
			RedisErrorHandler([&]()
			{
				freeReplyObject(reply);
			});

			return QTSS_NotConnected;
		}

		string easycms = Format("%s:", string(EASY_DARWIN_REDIS_EASYCMS));
		easycms += replyHmget->element[0]->str;;
		freeReplyObject(replyHmget);

		strTemp = Format("hmget %s %s %s ", easycms, string(EASY_DARWIN_REDIS_IP), string(EASY_DARWIN_REDIS_PORT));
		auto replyHmgetEasyDarwin = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
		if (!replyHmgetEasyDarwin)
		{
			RedisErrorHandler([&]()
			{
				freeReplyObject(reply);
			});

			return QTSS_NotConnected;
		}

		if (replyHmgetEasyDarwin->type == EASY_REDIS_REPLY_NIL)
		{
			freeReplyObject(replyHmgetEasyDarwin);
			freeReplyObject(reply);

			return QTSS_RequestFailed;
		}

		if (replyHmgetEasyDarwin->type == EASY_REDIS_REPLY_ARRAY && replyHmgetEasyDarwin->elements == 2)
		{
			memcpy(inParams->outCMSIP, replyHmgetEasyDarwin->element[0]->str, replyHmgetEasyDarwin->element[0]->len);
			memcpy(inParams->outCMSPort, replyHmgetEasyDarwin->element[1]->str, replyHmgetEasyDarwin->element[1]->len);
		}
		freeReplyObject(replyHmgetEasyDarwin);
	}

	freeReplyObject(reply);
	return QTSS_NoErr;
}

QTSS_Error RedisSetRTSPLoad()
{
	OSMutexLocker mutexLock(&sMutex);
	if (!sIfConSucess)
		return QTSS_NotConnected;

	char chKey[128] = { 0 };
	auto server = QTSServerInterface::GetServer()->GetServerName().Ptr;
	auto guid = QTSServerInterface::GetServer()->GetCloudServiceNodeID();
	auto load = QTSServerInterface::GetServer()->GetNumRTPSessions();

	sprintf(chKey, "expire %s:%s 15", server, guid);
	auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));

	if (!reply)
	{
		RedisErrorHandler([&]() {});

		return QTSS_NotConnected;
	}

	if (reply->integer == 1)
	{
		sprintf(chKey, "hset %s:%s %s %d", server, guid, EASY_DARWIN_REDIS_LOAD, load);
		auto replyHset = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
		if (!replyHset)
		{
			RedisErrorHandler([&]() {});
			return QTSS_NotConnected;
		}
		freeReplyObject(replyHset);
	}
	freeReplyObject(reply);

	return QTSS_NoErr;
}

QTSS_Error RedisJudgeStreamID(QTSS_JudgeStreamID_Params* inParams)
{
	////�㷨������ɾ��ָ��sessionID��Ӧ��key������ɹ�ɾ��������SessionID���ڣ���֤ͨ����������֤ʧ��
	//OSMutexLocker mutexLock(&sMutex);
	//if (!sIfConSucess)
	//	return QTSS_NotConnected;

	//char chKey[128] = { 0 };
	//sprintf(chKey, "SessionID_%s", inParams->inStreanID);//���key�����򷵻���������1�����򷵻���������0

	//int ret = sRedisClient->Delete(chKey);

	//if (ret == -1)//fatal err,need reconnect
	//{
	//	sRedisClient->Free();
	//	sIfConSucess = false;

	//	return QTSS_NotConnected;
	//}
	//else if (ret == 0)
	//{
	//	*(inParams->outresult) == 1;
	//	return QTSS_NoErr;
	//}
	//else
	//{
	//	return ret;
	//}
	return 0;
}

static void RedisErrorHandler(function<void()> func)
{
	printf("Connection error: %s\n", redisContext_->errstr);

	sIfConSucess = false;
	redisFree(redisContext_);

	func();
}
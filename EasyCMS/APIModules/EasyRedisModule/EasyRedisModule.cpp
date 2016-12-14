#include "EasyRedisModule.h"

#include "OSHeaders.h"
#include "QTSSModuleUtils.h"
#include "EasyRedisClient.h"
#include "QTSServerInterface.h"
#include "HTTPSessionInterface.h"
#include "Format.h"
#include "EasyUtil.h"

#include <stdio.h>

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
// Redis user
static char*            sRedisUser = nullptr;
static char*            sDefaultRedisUser = "admin";
// Redis password
static char*            sRedisPassword = nullptr;
static char*            sDefaultRedisPassword = "admin";
// EasyCMS
static char*			sCMSIP = nullptr;
static UInt16			sCMSPort = 10000;
static EasyRedisClient* sRedisClient = nullptr;//the object pointer that package the redis operation
static bool				sIfConSucess = false;
static OSMutex			sMutex;

// FUNCTION PROTOTYPES
static QTSS_Error EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock);
static QTSS_Error Register(QTSS_Register_Params* inParams);
static QTSS_Error Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error RereadPrefs();

static QTSS_Error RedisConnect();
static QTSS_Error RedisInit();
static QTSS_Error RedisTTL();
static QTSS_Error RedisAddDevName(QTSS_StreamName_Params* inParams);
static QTSS_Error RedisDelDevName(QTSS_StreamName_Params* inParams);
static QTSS_Error RedisGetAssociatedDarwin(QTSS_GetAssociatedDarwin_Params* inParams);
static QTSS_Error RedisGetBestDarwin(QTSS_GetBestDarwin_Params * inParams);
static QTSS_Error RedisGenStreamID(QTSS_GenStreamID_Params* inParams);

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
	case Easy_RedisAddDevName_Role:
		return RedisAddDevName(&inParamBlock->StreamNameParams);
	case Easy_RedisDelDevName_Role:
		return RedisDelDevName(&inParamBlock->StreamNameParams);
	case Easy_RedisTTL_Role:
		return RedisTTL();
	case Easy_RedisGetEasyDarwin_Role:
		return RedisGetAssociatedDarwin(&inParamBlock->GetAssociatedDarwinParams);
	case Easy_RedisGetBestEasyDarwin_Role:
		return RedisGetBestDarwin(&inParamBlock->GetBestDarwinParams);
	case Easy_RedisGenStreamID_Role:
		return RedisGenStreamID(&inParamBlock->GenStreamIDParams);
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
	(void)QTSS_AddRole(Easy_RedisAddDevName_Role);
	(void)QTSS_AddRole(Easy_RedisDelDevName_Role);
	(void)QTSS_AddRole(Easy_RedisGetEasyDarwin_Role);
	(void)QTSS_AddRole(Easy_RedisGetBestEasyDarwin_Role);
	(void)QTSS_AddRole(Easy_RedisGenStreamID_Role);
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

	sRedisClient = new EasyRedisClient();

	RedisConnect();

	return QTSS_NoErr;
}

QTSS_Error RereadPrefs()
{
	delete[] sRedis_IP;
	sRedis_IP = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_ip", sDefaultRedis_IP_Addr);

	QTSSModuleUtils::GetAttribute(modulePrefs, "redis_port", qtssAttrDataTypeUInt16, &sRedisPort, &sDefaultRedisPort, sizeof(sRedisPort));

	delete[] sRedisUser;
	sRedisUser = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_user", sDefaultRedisUser);

	delete[] sRedisPassword;
	sRedisPassword = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_password", sDefaultRedisPassword);

	//get cms ip and port
	delete[] sCMSIP;
	(void)QTSS_GetValueAsString(sServerPrefs, qtssPrefsMonitorWANIPAddr, 0, &sCMSIP);

	UInt32 len = sizeof(SInt32);
	(void)QTSS_GetValue(sServerPrefs, qtssPrefsMonitorWANPort, 0, static_cast<void*>(&sCMSPort), &len);

	return QTSS_NoErr;
}

QTSS_Error RedisConnect()
{
	if (sIfConSucess)
		return QTSS_NoErr;

	std::size_t timeout = 1;//timeout second
	if (sRedisClient->ConnectWithTimeOut(sRedis_IP, sRedisPort, timeout) == EASY_REDIS_OK)//return 0 if connect sucess
	{
		qtss_printf("Connect redis sucess\n");
		sIfConSucess = true;
		std::size_t timeoutSocket = 1;//timeout socket second
		sRedisClient->SetTimeout(timeoutSocket);
		RedisInit();
	}
	else
	{
		qtss_printf("Connect redis failed\n");
		sIfConSucess = false;
	}

	if (sIfConSucess)
	{
		return QTSS_NoErr;
	}
	else
	{
		return QTSS_NotConnected;
	}
}

QTSS_Error RedisInit()//only called by RedisConnect after connect redis sucess
{
	//ÿһ����redis���Ӻ󣬶�Ӧ�������һ�ε����ݴ洢��ʹ�ø��ǻ���ֱ������ķ�ʽ,��������ʹ�ù��߸��Ӹ�Ч
	char chTemp[128] = { 0 };

	do
	{
		//1,redis������֤
		sprintf(chTemp, "auth %s", sRedisPassword);
		sRedisClient->AppendCommand(chTemp);

		auto id = QTSServerInterface::GetServer()->GetCloudServiceNodeID();
		sprintf(chTemp, "hmset EasyCMS:%s IP %s Port %d Load %d", id, sCMSIP, sCMSPort, 0);
		sRedisClient->AppendCommand(chTemp);

		OSRefTableEx*  deviceRefTable = QTSServerInterface::GetServer()->GetDeviceSessionMap();
		OSMutex *mutexMap = deviceRefTable->GetMutex();
		OSHashMap  *deviceMap = deviceRefTable->GetMap();
		OSRefIt itRef;
		{
			OSMutexLocker lock(mutexMap);
			for (itRef = deviceMap->begin(); itRef != deviceMap->end(); ++itRef)
			{
				auto deviceInfo = static_cast<HTTPSessionInterface*>(itRef->second->GetObjectPtr())->GetDeviceInfo();

				string type, channel;
				if (deviceInfo->eAppType == EASY_APP_TYPE_CAMERA)
				{
					type = "EasyCamera";
				}
				else if (deviceInfo->eAppType == EASY_APP_TYPE_NVR)
				{
					type = "EasyNVR";
					auto channels = deviceInfo->channels_;
					for (auto& item : channels)
					{
						channel += item.first;
					}
				}

				sprintf(chTemp, "hmset Device:%s Type %s Channel %s EasyCMS %s Token %s", deviceInfo->serial_.c_str(),
					type.c_str(), channel.c_str(), id, deviceInfo->password_.c_str());

				sRedisClient->AppendCommand(chTemp);
			}
		}

		bool bBreak = false;
		easyRedisReply* reply = nullptr;
		for (int i = 0; i < deviceMap->size() + 2; i++)
		{
			if (EASY_REDIS_OK != sRedisClient->GetReply(reinterpret_cast<void**>(&reply)))
			{
				bBreak = true;
				if (reply)
					EasyFreeReplyObject(reply);
				break;
			}
			EasyFreeReplyObject(reply);
		}
		if (bBreak)//˵��redisGetReply�����˴���
			break;
		return QTSS_NoErr;
	} while (false);
	//�ߵ���˵�������˴�����Ҫ��������,������������һ��ִ������ʱ����,����������ñ�־λ
	sRedisClient->Free();

	sIfConSucess = false;
	return QTSS_RequestFailed;
}

QTSS_Error RedisAddDevName(QTSS_StreamName_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);
	if (!sIfConSucess)
		return QTSS_NotConnected;

	char chKey[128] = { 0 };
	sprintf(chKey, "%s:%d_DevName", sCMSIP, sCMSPort);

	int ret = sRedisClient->SAdd(chKey, inParams->inStreamName);
	if (ret == -1)//fatal err,need reconnect
	{
		sRedisClient->Free();
		sIfConSucess = false;
	}

	return ret;
}

QTSS_Error RedisDelDevName(QTSS_StreamName_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);
	if (!sIfConSucess)
		return QTSS_NotConnected;

	char chKey[128] = { 0 };
	sprintf(chKey, "%s:%d_DevName", sCMSIP, sCMSPort);

	int ret = sRedisClient->SRem(chKey, inParams->inStreamName);
	if (ret == -1)//fatal err,need reconnect
	{
		sRedisClient->Free();
		sIfConSucess = false;
	}

	return ret;
}

QTSS_Error RedisTTL()//ע�⵱������һ��ʱ��ܲ�ʱ���ܻ���Ϊ��ʱʱ��ﵽ������key��ɾ������ʱӦ���������ø�key
{
	OSMutexLocker mutexLock(&sMutex);

	if (RedisConnect() != QTSS_NoErr)//ÿһ��ִ������֮ǰ��������redis,�����ǰredis��û�гɹ�����
		return QTSS_NotConnected;

	char chKey[128] = { 0 };//ע��128λ�Ƿ��㹻
	sprintf(chKey, "%s:%s 15", QTSServerInterface::GetServerName().Ptr, QTSServerInterface::GetServer()->GetCloudServiceNodeID());//���ĳ�ʱʱ��

	int ret = sRedisClient->SetExpire(chKey, 15);
	if (ret == -1)//fatal error
	{
		sRedisClient->Free();
		sIfConSucess = false;
		return QTSS_NotConnected;
	}
	else if (ret == 1)
	{
		return QTSS_NoErr;
	}
	else if (ret == 0)//the key doesn't exist, reset
	{
		char chTemp[128]{ 0 };
		auto id = QTSServerInterface::GetServer()->GetCloudServiceNodeID();
		sprintf(chTemp, "hmset EasyCMS:%s IP %s Port %d Load %d", id, sCMSIP, sCMSPort, 0);
		sRedisClient->AppendCommand(chTemp);

		easyRedisReply* reply = nullptr;
		auto re = sRedisClient->GetReply(reinterpret_cast<void**>(&reply));
		EasyFreeReplyObject(reply);
		//sprintf(chKey, "%s:%d_Live", sCMSIP, sCMSPort);
		//int retret = sRedisClient->SetEX(chKey, 15, "1");
		//if (retret == -1)//fatal error
		//{
		//	sRedisClient->Free();
		//	sIfConSucess = false;
		//}
		//return retret;

		return ret;
	}
	else
	{
		return ret;
	}
}

QTSS_Error RedisGetAssociatedDarwin(QTSS_GetAssociatedDarwin_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	if (!sIfConSucess)
		return QTSS_NotConnected;

	string strPushName = Format("%s/%s", string(inParams->inSerial), string(inParams->inChannel));

	//1. get the list of EasyDarwin
	easyRedisReply * reply = static_cast<easyRedisReply*>(sRedisClient->SMembers("EasyDarwinName"));
	if (reply == nullptr)
	{
		sRedisClient->Free();
		sIfConSucess = false;
		return QTSS_NotConnected;
	}

	//2.judge if the EasyDarwin is ilve and contain serial/channel.sdp
	if ((reply->elements > 0) && (reply->type == EASY_REDIS_REPLY_ARRAY))
	{
		easyRedisReply* childReply;
		for (size_t i = 0; i < reply->elements; i++)
		{
			childReply = reply->element[i];
			string strChileReply(childReply->str);

			string strTemp = Format("exists %s", strChileReply + "_Live");
			sRedisClient->AppendCommand(strTemp.c_str());

			strTemp = Format("sismember %s %s", strChileReply + "_PushName", strPushName);
			sRedisClient->AppendCommand(strTemp.c_str());
		}

		easyRedisReply *reply2 = nullptr, *reply3 = nullptr;
		for (size_t i = 0; i < reply->elements; i++)
		{
			if (sRedisClient->GetReply(reinterpret_cast<void**>(&reply2)) != EASY_REDIS_OK)
			{
				EasyFreeReplyObject(reply);
				if (reply2)
				{
					EasyFreeReplyObject(reply2);
				}
				sRedisClient->Free();
				sIfConSucess = false;
				return QTSS_NotConnected;
			}
			if (sRedisClient->GetReply(reinterpret_cast<void**>(&reply3)) != EASY_REDIS_OK)
			{
				EasyFreeReplyObject(reply);
				if (reply3)
				{
					EasyFreeReplyObject(reply3);
				}
				sRedisClient->Free();
				sIfConSucess = false;
				return QTSS_NotConnected;
			}

			if ((reply2->type == EASY_REDIS_REPLY_INTEGER) && (reply2->integer == 1) &&
				(reply3->type == EASY_REDIS_REPLY_INTEGER) && (reply3->integer == 1))
			{//find it
				string strIpPort(reply->element[i]->str);
				int ipos = strIpPort.find(':');//judge error
				memcpy(inParams->outDssIP, strIpPort.c_str(), ipos);
				memcpy(inParams->outDssPort, &strIpPort[ipos + 1], strIpPort.size() - ipos - 1);
				//break;//can't break,as 1 to 1
			}
			EasyFreeReplyObject(reply2);
			EasyFreeReplyObject(reply3);
		}
	}
	EasyFreeReplyObject(reply);
	return QTSS_NoErr;
}

QTSS_Error RedisGetBestDarwin(QTSS_GetBestDarwin_Params * inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	QTSS_Error theErr = QTSS_NoErr;

	if (!sIfConSucess)
		return QTSS_NotConnected;


	char chTemp[128] = { 0 };

	//1. get the list of EasyDarwin
	easyRedisReply * reply = static_cast<easyRedisReply *>(sRedisClient->SMembers("EasyDarwinName"));
	if (reply == nullptr)
	{
		sRedisClient->Free();
		sIfConSucess = false;
		return QTSS_NotConnected;
	}

	//2.judge if the EasyDarwin is ilve and get the RTP
	if ((reply->elements > 0) && (reply->type == EASY_REDIS_REPLY_ARRAY))
	{
		easyRedisReply* childReply;
		for (size_t i = 0; i < reply->elements; i++)
		{
			childReply = reply->element[i];
			string strChileReply(childReply->str);

			sprintf(chTemp, "exists %s", (strChileReply + "_Live").c_str());
			sRedisClient->AppendCommand(chTemp);

			sprintf(chTemp, "hget %s %s", (strChileReply + "_Info").c_str(), "RTP");
			sRedisClient->AppendCommand(chTemp);
		}

		int key = -1, keynum = 0;
		easyRedisReply *reply2 = nullptr, *reply3 = nullptr;
		for (size_t i = 0; i < reply->elements; i++)
		{
			if (sRedisClient->GetReply(reinterpret_cast<void**>(&reply2)) != EASY_REDIS_OK)
			{
				EasyFreeReplyObject(reply);
				if (reply2)
				{
					EasyFreeReplyObject(reply2);
				}
				sRedisClient->Free();
				sIfConSucess = false;
				return QTSS_NotConnected;
			}

			if (sRedisClient->GetReply(reinterpret_cast<void**>(&reply3)) != EASY_REDIS_OK)
			{
				EasyFreeReplyObject(reply);
				if (reply3)
				{
					EasyFreeReplyObject(reply3);
				}
				sRedisClient->Free();
				sIfConSucess = false;
				return QTSS_NotConnected;
			}

			if ((reply2->type == EASY_REDIS_REPLY_INTEGER) && (reply2->integer == 1) &&
				(reply3->type == EASY_REDIS_REPLY_STRING))
			{//find it
				int RTPNum = atoi(reply3->str);
				if (key == -1)
				{
					key = i;
					keynum = RTPNum;
				}
				else
				{
					if (RTPNum < keynum)//find better
					{
						key = i;
						keynum = RTPNum;
					}
				}
			}
			EasyFreeReplyObject(reply2);
			EasyFreeReplyObject(reply3);
		}
		if (key == -1)//no one live
		{
			theErr = QTSS_Unimplemented;
		}
		else
		{
			string strIpPort(reply->element[key]->str);
			int ipos = strIpPort.find(':');//judge error
			memcpy(inParams->outDssIP, strIpPort.c_str(), ipos);
			memcpy(inParams->outDssPort, &strIpPort[ipos + 1], strIpPort.size() - ipos - 1);
		}
	}
	else//û�п��õ�EasyDarWin
	{
		theErr = QTSS_Unimplemented;
	};
	EasyFreeReplyObject(reply);
	return theErr;
}

QTSS_Error RedisGenStreamID(QTSS_GenStreamID_Params* inParams)
{
	//�㷨���٣��������sessionID����redis���Ƿ��д洢��û�оʹ���redis�ϣ��еĻ��������ɣ�ֱ��û��Ϊֹ
	OSMutexLocker mutexLock(&sMutex);

	if (!sIfConSucess)
		return QTSS_NotConnected;

	easyRedisReply* reply = nullptr;
	char chTemp[128] = { 0 };
	string strSessioionID;

	do
	{
		if (reply)//�ͷ���һ����Ӧ
			EasyFreeReplyObject(reply);

		strSessioionID = OSMapEx::GenerateSessionIdForRedis(sCMSIP, sCMSPort);

		sprintf(chTemp, "SessionID_%s", strSessioionID.c_str());
		reply = static_cast<easyRedisReply*>(sRedisClient->Exists(chTemp));
		if (nullptr == reply)//������Ҫ��������
		{
			sRedisClient->Free();
			sIfConSucess = false;
			return QTSS_NotConnected;
		}
	} while ((reply->type == EASY_REDIS_REPLY_INTEGER) && (reply->integer == 1));
	EasyFreeReplyObject(reply);//�ͷ����һ���Ļ�Ӧ

	//�ߵ���˵���ҵ���һ��Ψһ��SessionID�����ڽ����洢��redis��
	sprintf(chTemp, "SessionID_%s", strSessioionID.c_str());//�߼��汾֧��setpx�����ó�ʱʱ��Ϊms
	if (sRedisClient->SetEX(chTemp, inParams->inTimeoutMil / 1000, "1") == -1)
	{
		sRedisClient->Free();
		sIfConSucess = false;
		return QTSS_NotConnected;
	}
	strcpy(inParams->outStreanID, strSessioionID.c_str());
	return QTSS_NoErr;
}
#include <stdio.h>

#include "EasyRedisModule.h"
#include "OSHeaders.h"
#include "QTSSModuleUtils.h"
#include "MyAssert.h"
#include "EasyRedisClient.h"
#include "QTSServerInterface.h"
#include "HTTPSessionInterface.h"

// STATIC VARIABLES
static QTSS_ModulePrefsObject modulePrefs = NULL;
static QTSS_PrefsObject     sServerPrefs    = NULL;
static QTSS_ServerObject    sServer     = NULL;

// Redis IP
static char*            sRedis_IP				= NULL;
static char*            sDefaultRedis_IP_Addr	= "127.0.0.1";
// Redis Port
static UInt16			sRedisPort				= 6379;
static UInt16			sDefaultRedisPort		= 6379;
// Redis user
static char*            sRedisUser				= NULL;
static char*            sDefaultRedisUser		= "admin";
// Redis password
static char*            sRedisPassword			= NULL;
static char*            sDefaultRedisPassword	= "admin";

// FUNCTION PROTOTYPES
static QTSS_Error   EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock);
static QTSS_Error   Register(QTSS_Register_Params* inParams);
static QTSS_Error   Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error   RereadPrefs();

static EasyRedisClient * sRedisClient = NULL;//the object pointer that package the redis operation
static bool sIfConSucess = false;
static char * sCMSIP = NULL;
static UInt16 sCMSPort = 10000;

static QTSS_Error RedisConnect();
static QTSS_Error RedisInit();


QTSS_Error EasyRedisModule_Main(void* inPrivateArgs)
{
	return _stublibrary_main(inPrivateArgs, EasyRedisModuleDispatch);
}

QTSS_Error  EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock)
{
	switch (inRole)
	{
	case QTSS_Register_Role:
		return Register(&inParamBlock->regParams);
	case QTSS_Initialize_Role:
		return Initialize(&inParamBlock->initParams);
	case QTSS_RereadPrefs_Role:
		return RereadPrefs();
	}
	return QTSS_NoErr;
}

QTSS_Error Register(QTSS_Register_Params* inParams)
{
	// Do role setup
	(void)QTSS_AddRole(QTSS_Initialize_Role);
	(void)QTSS_AddRole(QTSS_RereadPrefs_Role);

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
	delete [] sRedis_IP;
    sRedis_IP = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_ip", sDefaultRedis_IP_Addr);

	QTSSModuleUtils::GetAttribute(modulePrefs, "redis_port", qtssAttrDataTypeUInt16, &sRedisPort, &sDefaultRedisPort, sizeof(sRedisPort));

	delete [] sRedisUser;
    sRedisUser = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_user", sDefaultRedisUser);
	
	delete [] sRedisPassword;
    sRedisPassword = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_password", sDefaultRedisPassword);
	
	delete [] sCMSIP;
	(void)QTSS_GetValueAsString(sServerPrefs, qtssPrefsMonitorWANIPAddr, 0, &sCMSIP);

	UInt32 len = sizeof(SInt32);
	(void) QTSS_GetValue(sServerPrefs, qtssPrefsMonitorWANPort, 0, (void*)&sCMSPort, &len);

	return QTSS_NoErr;
}

QTSS_Error RedisConnect()
{
	if(sIfConSucess)
		return QTSS_NoErr;

	std::size_t timeout = 1;//timeout second
	if(sRedisClient->ConnectWithTimeOut(sRedis_IP,sRedisPort,timeout) != -1)
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
	return (QTSS_Error)sIfConSucess;
}

QTSS_Error RedisInit()//only called by RedisConnect after connect redis sucess
{
	//ÿһ����redis���Ӻ󣬶�Ӧ�������һ�ε����ݴ洢��ʹ�ø��ǻ���ֱ������ķ�ʽ,��������ʹ�ù��߸��Ӹ�Ч
	char chTemp[128]={0};

	do 
	{
		//1,redis������֤
		sprintf(chTemp,"auth %s",sRedisPassword);
		sRedisClient->AppendCommand(chTemp);

		//2,CMSΨһ��Ϣ�洢(������һ�εĴ洢)
		sprintf(chTemp,"sadd CMSName %s:%d",sCMSIP,sCMSPort);
		sRedisClient->AppendCommand(chTemp);


		//3,CMS���Դ洢,���ö��filedʹ��hmset������ʹ��hset(������һ�εĴ洢)
		sprintf(chTemp,"hmset %s:%d_Info IP %s PORT %d",sCMSIP,sCMSPort,sCMSIP,sCMSPort);
		sRedisClient->AppendCommand(chTemp);

		//4,����豸���ƴ洢����Ϊ����֮ǰ������֮����豸����һ��÷����˱仯����˱�����ִ���������
		sprintf(chTemp,"del %s:%d_DevName",sCMSIP,sCMSPort);
		sRedisClient->AppendCommand(chTemp);

		OSRefTableEx*  deviceRefTable = QTSServerInterface::GetServer()->GetDeviceSessionMap();
		OSMutex *mutexMap = deviceRefTable->GetMutex();
		OSHashMap  *deviceMap = deviceRefTable->GetMap();
		OSRefIt itRef;
		string strAllDevices;
		mutexMap->Lock();
		for(itRef = deviceMap->begin();itRef != deviceMap->end();itRef++)
		{
			strDevice *deviceInfo=(((HTTPSessionInterface*)(itRef->second->GetObjectPtr()))->GetDeviceInfo());
			strAllDevices=strAllDevices+' '+deviceInfo->serial_;
		}
		mutexMap->Unlock();

		char *chNewTemp=new char[strAllDevices.size()+128];//ע�⣬���ﲻ����ʹ��chTemp����Ϊ���Ȳ�ȷ�������ܵ��»��������
		//5,�豸���ƴ洢
		sprintf(chNewTemp,"sadd %s:%d_DevName%s",sCMSIP,sCMSPort,strAllDevices.c_str());
		sRedisClient->AppendCommand(chNewTemp);
		delete[] chNewTemp;

		//6,�������15�룬��֮��ǰCMS�Ѿ���ʼ�ṩ������
		sprintf(chTemp,"setex %s:%d_Live 15 1",sCMSIP,sCMSPort);
		sRedisClient->AppendCommand(chTemp);

		bool bBreak=false;
		redisReply* reply = NULL;
		for(int i=0;i<6;i++)
		{
			if(REDIS_OK != sRedisClient->GetReply((void**)&reply))
			{
				bBreak=true;
				freeReplyObject(reply);
				break;
			}
			freeReplyObject(reply);
		}
		if(bBreak)//˵��redisGetReply�����˴���
			break;
		return QTSS_NoErr;
	} while (0);
	//�ߵ���˵�������˴�����Ҫ��������,������������һ��ִ������ʱ����,����������ñ�־λ
	sRedisClient->Free();

	sIfConSucess=false;
	return (QTSS_Error)false;
}
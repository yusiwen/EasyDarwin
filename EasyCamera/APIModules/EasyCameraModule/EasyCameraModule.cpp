/*
	Copyright (c) 2013-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
    File:       EasyCMSModule.cpp
    Contains:   Implementation of EasyCMSModule class. 
*/

#include "EasyCameraModule.h"
#include "QTSSModuleUtils.h"
#include "OSRef.h"
#include "StringParser.h"
#include "QTSServerInterface.h"

#include "EasyCameraSource.h"


// STATIC DATA
static QTSS_PrefsObject				sServerPrefs			= NULL;	//������������
static QTSS_ServerObject			sServer					= NULL;	//����������
static QTSS_ModulePrefsObject		sEasyCameraModulePrefs	= NULL;	//��ǰģ������

static EasyCameraSource*			sCameraSource			= NULL; //ΨһEasyCameraSource����

// FUNCTION PROTOTYPES
static QTSS_Error EasyCameraModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams);
static QTSS_Error Register_EasyCameraModule(QTSS_Register_Params* inParams);
static QTSS_Error Initialize_EasyCameraModule(QTSS_Initialize_Params* inParams);
static QTSS_Error RereadPrefs_EasyCameraModule();


// FUNCTION IMPLEMENTATIONS
QTSS_Error EasyCameraModule_Main(void* inPrivateArgs)
{
    return _stublibrary_main(inPrivateArgs, EasyCameraModuleDispatch);
}

QTSS_Error  EasyCameraModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams)
{
    switch (inRole)
    {
        case QTSS_Register_Role:
            return Register_EasyCameraModule(&inParams->regParams);
        case QTSS_Initialize_Role:
            return Initialize_EasyCameraModule(&inParams->initParams);
        case QTSS_RereadPrefs_Role:
            return RereadPrefs_EasyCameraModule();
	}
    return QTSS_NoErr;
}

QTSS_Error Register_EasyCameraModule(QTSS_Register_Params* inParams)
{
    // Do role & attribute setup
    (void)QTSS_AddRole(QTSS_Initialize_Role);
    (void)QTSS_AddRole(QTSS_RereadPrefs_Role);
   
    // Tell the server our name!
    static char* sModuleName = "EasyCameraModule";
    ::strcpy(inParams->outModuleName, sModuleName);

    return QTSS_NoErr;
}

QTSS_Error Initialize_EasyCameraModule(QTSS_Initialize_Params* inParams)
{
    // Setup module utils
    QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);

    // Setup global data structures
    sServerPrefs = inParams->inPrefs;
    sServer = inParams->inServer;
    sEasyCameraModulePrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);

	//��ȡEasyCMSModule����
	RereadPrefs_EasyCameraModule();

	//��������ʼEasyCMSSession����
	sCameraSource = new EasyCameraSource();
	sCameraSource->Signal(Task::kStartEvent);

    return QTSS_NoErr;
}

QTSS_Error RereadPrefs_EasyCameraModule()
{
	return QTSS_NoErr;
}
#include "RtspRecordModule.h"
#include "QTSSModuleUtils.h"
#include "QTSSMemoryDeleter.h"
#include "RtspRecordSession.h"


// FUNCTION PROTOTYPES
static QTSS_Error RtspRecordModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams);
static QTSS_Error Register(QTSS_Register_Params* inParams);
static QTSS_Error Initialize(QTSS_Initialize_Params* inParams);

// FUNCTION IMPLEMENTATIONS
QTSS_Error RtspRecordModule_Main(void* inPrivateArgs)
{
	return _stublibrary_main(inPrivateArgs, RtspRecordModuleDispatch);
}

QTSS_Error  RtspRecordModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParams)
{
	switch (inRole)
	{
	case QTSS_Register_Role:
		return Register(&inParams->regParams);
	case QTSS_Initialize_Role:
		return Initialize(&inParams->initParams);
	}
	return QTSS_NoErr;
}

QTSS_Error Initialize(QTSS_Initialize_Params* inParams)
{
	// Setup module utils
	QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);

	QTSS_ModulePrefsObject sModulePrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);

	// Call helper class initializers
	RtspRecordSession::Initialize(sModulePrefs);

	return QTSS_NoErr;
}

QTSS_Error Register(QTSS_Register_Params* inParams)
{
	// Do role & attribute setup
	(void)QTSS_AddRole(QTSS_Initialize_Role);
	static char* sModuleName = "RtspRecordModule";
	::strcpy(inParams->outModuleName, sModuleName);
	return QTSS_NoErr;
}
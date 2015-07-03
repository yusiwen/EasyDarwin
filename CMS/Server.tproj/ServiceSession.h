/*
	Copyleft (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
/*! 
  \file    ServiceSession.h  
  \author  Babosa@EasyDarwin.org
  \date    2014-12-03
  \version 1.0
  \mainpage ʹ������
  
  ���������Ҫ����\n
  Select -> ServiceSession -> DispatchMsgCenter -> ServiceSession -> Cleanup\n\n

  Copyright (c) 2014 EasyDarwin.org ��Ȩ����\n  
 
  \defgroup ����Ԫ�����¼���������
*/

#ifndef __SERVICESESSION_H__
#define __SERVICESESSION_H__

#include "BaseSessionInterface.h"
#include "HTTPRequest.h"
#include "TimeoutTask.h"
#include "QTSSModule.h"

#include "EasyDSSProtocolDef.h"
#include "EasyDSSProtocol.h"

using namespace EasyDSS::Protocol;
using namespace std;

class CServiceSession : public BaseSessionInterface
{
    public:
        CServiceSession();
        virtual ~CServiceSession();
		
		////����HTTP��Ӧ����
		virtual QTSS_Error SendHTTPPacket(StrPtrLen* contentXML, Bool16 connectionClose, Bool16 decrement);

    private: 
        SInt64 Run();

        // Does request prep & request cleanup, respectively
        QTSS_Error SetupRequest();
        void CleanupRequest();
		
		QTSS_Error ExecNetMsgDevRegisterReq(const char* json);
		QTSS_Error ExecNetMsgNgxStreamReq(const char* json);
		QTSS_Error ExecNetMsgDefaultReqHandler(const char* json);
		QTSS_Error ExecNetMsgSnapUpdateReq(const char* json);

        
        // test current connections handled by this object against server pref connection limit
        Bool16 OverMaxConnections(UInt32 buffer);

        HTTPRequest*        fRequest;
        OSMutex             fReadMutex;

		//���籨�Ĵ���״̬��
        enum
        {
            kReadingRequest             = 0,	//��ȡ����
            kFilteringRequest           = 1,	//���˱���
            kPreprocessingRequest       = 2,	//Ԥ������
            kProcessingRequest          = 3,	//������
            kSendingResponse            = 4,	//������Ӧ����
            kCleaningUp                 = 5,	//��ձ��δ���ı�������
        
            kReadingFirstRequest		= 6,	//��һ�ζ�ȡSession���ģ���Ҫ������SessionЭ�����֣�HTTP/TCP/RTSP�ȵȣ�
            kHaveCompleteMessage		= 7    // ��ȡ�������ı���
        };
        
        UInt32 fCurrentModule;
        UInt32 fState;

        QTSS_RoleParams     fRoleParams;//module param blocks for roles.
        QTSS_ModuleState    fModuleState;

		//���������
        QTSS_Error DumpRequestData();

};
#endif // __SERVICESESSION_H__


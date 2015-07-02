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

#ifndef __Http_Session_H__
#define __Http_Session_H__

#include "HttpSessionInterface.h"
#include "HTTPRequest.h"
#include "TimeoutTask.h"
#include "QTSSModule.h"

class CHttpSession : public HttpSessionInterface
{
    public:
        CHttpSession();
        virtual ~CHttpSession();
		
	//����HTTP  ����
	virtual QTSS_Error SendHTTPPacket(StrPtrLen* content, Bool16 connectionClose);

    private: 
        SInt64 Run();

        // Does request prep & request cleanup, respectively
        QTSS_Error SetupRequest();
        void CleanupRequest();

	 // test current connections handled by this object against server pref connection limit
	 Bool16 OverMaxConnections(UInt32 buffer);

        HTTPRequest*        fRequest;
        
        OSMutex             	fReadMutex;

	 //���籨�Ĵ���״̬��
        enum
        {
		kReadingRequestHead		= 0,
		kReadingRequestBody		= 1,		
		kProcessingRequest		= 2,
		kSendingResponse		= 3,
		kCleaningUp				= 4,
        };
        
        UInt32 fCurrentModule;
        UInt32 fState;

        QTSS_RoleParams     fRoleParams;//module param blocks for roles.
        QTSS_ModuleState    fModuleState;

	//���������
	QTSS_Error DumpRequestData();

};

#endif // __Http_Session_H__


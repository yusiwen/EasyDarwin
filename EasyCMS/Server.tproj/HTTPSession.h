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

#ifndef __HTTP_SESSION_H__
#define __HTTP_SESSION_H__

#include "HTTPSessionInterface.h"
#include "HTTPRequest.h"
#include "TimeoutTask.h"
#include "QTSSModule.h"

using namespace std;


class HTTPSession : public HTTPSessionInterface
{
    public:
        HTTPSession();
        virtual ~HTTPSession();
		
		////����HTTP��Ӧ����
		virtual QTSS_Error SendHTTPPacket(StrPtrLen* contentXML, Bool16 connectionClose, Bool16 decrement);

		char* GetDeviceSnap(){ return fDeviceSnap; };
		char* GetDeviceSerial(){ return (char*)fDevSerial.c_str(); };
		
		void SetStreamPushInfo(EasyJsonValue &info) { fStreamPushInfo = info; }
		EasyJsonValue &GetStreamPushInfo() { return fStreamPushInfo; }

		

    private: 
        SInt64 Run();

        // Does request prep & request cleanup, respectively
        QTSS_Error SetupRequest();
        void CleanupRequest();
		
		//������begin
		QTSS_Error ExecNetMsgDevRegisterReq(const char* json);
		QTSS_Error ExecNetMsgGetDeviceListReq(char *queryString);
		QTSS_Error ExecNetMsgGetCameraListReq(const string& device_serial, char* queryString);
		QTSS_Error ExecNetMsgStartStreamReq(const string& device_serial, char* queryString);
		QTSS_Error ExecNetMsgStopStreamReq(const string& device_serial, char* queryString);
		QTSS_Error ExecNetMsgStartDeviceStreamRsp(const char* json);
		QTSS_Error ExecNetMsgStopDeviceStreamRsp(const char* json);
		//������end

		
		QTSS_Error ProcessRequest();//�������󣬵����ŵ�һ��״̬��ȥ�������������ظ�ִ��
		QTSS_Error ExecNetMsgDefaultReqHandler(const char* json);//��ϢĬ�ϴ�����
		QTSS_Error ExecNetMsgDevRegisterReqEx(const char* json);//�豸ע������
		QTSS_Error ExecNetMsgStreamStartReqRestful(char *queryString);//�ͻ�����������Restful�ӿ�
		QTSS_Error ExecNetMsgStreamStartReq(const char* json);//�ͻ�����������
		QTSS_Error ExecNetMsgStartDeviceStreamRspEx(const char* json);//�豸�Ŀ�ʼ����Ӧ
		QTSS_Error ExecNetMsgStreamStopReqRestful(char *queryString);//�ͻ��˵�ֱֹͣ������Restful�ӿ�
		QTSS_Error ExecNetMsgStreamStopReq(const char *json);//�ͻ��˵�ֱֹͣ������
		QTSS_Error ExecNetMsgStreamStopRsp(const char* json);//�豸��ֹͣ������Ӧ
		QTSS_Error ExecNetMsgSnapUpdateReq(const char* json);//�豸�Ŀ��ո�������
		QTSS_Error ExecNetMsgGetDeviceListReqEx(char *queryString);//�ͻ��˻���豸�б�,restful�ӿ�
		QTSS_Error ExecNetMsgGetDeviceListReqJsonEx(const char *json);//�ͻ��˻���豸�б�json�ӿ�
		QTSS_Error ExecNetMsgGetCameraListReqEx(char *queryString);//�ͻ��˻������ͷ�б�restful�ӿڣ������豸����ΪNVRʱ��Ч
		QTSS_Error ExecNetMsgGetCameraListReqJsonEx(const char *json);//�ͻ��˻������ͷ�б�json�ӿ�,�����豸����ΪNVRʱ��Ч


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

		char* fDeviceSnap;
		EasyJsonValue fStreamPushInfo;
};
#endif // __HTTP_SESSION_H__


/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#ifndef EASY_PROTOCOL_H
#define	EASY_PROTOCOL_H


#include <EasyProtocolBase.h>
#include <map>
#include <vector>
#include <boost/variant.hpp>
#include <boost/lexical_cast.hpp>
#include <list>
#include <set>
using namespace std;


namespace EasyDarwin { namespace Protocol
{

class EASYDARWIN_API EasyDevice//����ͷ��Ϣ��
{
public:
	EasyDevice();
	EasyDevice(string serial, string name);
	EasyDevice(string serial, string name, string status);
	~EasyDevice(){}

public:
	string serial_;
	string name_;
	string status_;//online/offline
	string password_;
	string tag_;
	string channel_;
};

typedef vector<EasyDevice> EasyDevices;//����ͷ����
typedef EasyDevices::iterator EasyDevicesIterator;

typedef boost::variant<int, float, string> value_t;
typedef map<string, value_t> EasyJsonValue;//keyΪstring,value������int��float��string��һ��
typedef void* EasyObject;


typedef struct
{
	string device_serial_;
	string camera_serial_;
	string stream_id_;
	EasyObject object_;
	unsigned long timestamp_;
	unsigned long timeout_;//second
	int message_type_;//see EasyDarwin_Message_Type_Define
}EasyNVRMessage;//NVR��Ϣ

typedef vector<EasyNVRMessage> EasyNVRMessageQueue;//NVR��Ϣ����


class EasyJsonValueVisitor : public boost::static_visitor<string>
{
public:
	string operator()(int value) const { return boost::lexical_cast<string>(value); }
	string operator()(float value) const { return boost::lexical_cast<string>(value); }
	string operator()(string value) const { return value; }
};

class EASYDARWIN_API EasyNVR : public EasyDevice//NVR����
{
public:
	EasyNVR();
	EasyNVR(string serial, string name, string password, string tag, EasyDevices &channel);
	~EasyNVR(){}

public:	
	EasyDevices channels_;                                                           
	EasyObject object_;
};

typedef map<string, EasyNVR> EasyNVRs;//ά�����е�NVR
class EASYDARWIN_API EasyDarwinRegisterReq : public EasyProtocol//��װNVR��ע������
{
public:
	EasyDarwinRegisterReq(EasyNVR &nvr, size_t cseq = 1);
	EasyDarwinRegisterReq(const char* msg);
	virtual ~EasyDarwinRegisterReq(){}

public:
	EasyNVR& GetNVR(){ return nvr_; }

private:
	EasyNVR nvr_;
};


class EASYDARWIN_API EasyDarwinRegisterRSP : public EasyProtocol//��װNVR��ע���Ӧ
{
public:
	EasyDarwinRegisterRSP(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinRegisterRSP(const char* msg);
	virtual ~EasyDarwinRegisterRSP(){}
};
class EasyDarwinDeviceStreamReq : public EasyProtocol
{
public:
	EasyDarwinDeviceStreamReq(EasyJsonValue &body, size_t cseq);
	EasyDarwinDeviceStreamReq(const char* msg);
	~EasyDarwinDeviceStreamReq(){}
};

class EasyDarwinDeviceStreamRsp : public EasyProtocol
{
public:
	EasyDarwinDeviceStreamRsp(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinDeviceStreamRsp(const char* msg);
	~EasyDarwinDeviceStreamRsp(){}
};



class EasyDarwinDeviceStreamStop : public EasyProtocol
{
public:
	EasyDarwinDeviceStreamStop(EasyJsonValue &body, size_t cseq = 1);
	EasyDarwinDeviceStreamStop(const char* msg);
	~EasyDarwinDeviceStreamStop(){}
};

class EasyDarwinDeviceStreamStopRsp : public EasyProtocol
{
public:
	EasyDarwinDeviceStreamStopRsp(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinDeviceStreamStopRsp(const char* msg);
	~EasyDarwinDeviceStreamStopRsp(){}
};

//nvr list response
class EasyDarwinDeviceListRsp : public EasyProtocol
{
public:
	EasyDarwinDeviceListRsp(EasyDevices &devices, size_t cseq = 1, size_t error = 200);
	EasyDarwinDeviceListRsp(const char* msg);
	~EasyDarwinDeviceListRsp(){}

	EasyDevices& GetDevices() { return devices_; }

private:
	EasyDevices devices_;
};

//camera list response
class EasyDarwinCameraListRsp : public EasyProtocol
{
public:
	EasyDarwinCameraListRsp(EasyDevices &cameras, string devcei_serial, size_t cseq = 1, size_t error = 200);
	EasyDarwinCameraListRsp(const char* msg);
	~EasyDarwinCameraListRsp() {}

	EasyDevices& GetCameras() { return cameras_; }

private:
	EasyDevices cameras_;
};

class EasyDarwinClientStartStreamRsp : public EasyProtocol
{
public:
	EasyDarwinClientStartStreamRsp(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinClientStartStreamRsp(const char* msg);
	~EasyDarwinClientStartStreamRsp(){}
};

class EasyDarwinClientStopStreamRsp : public EasyProtocol
{
public:
	EasyDarwinClientStopStreamRsp(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinClientStopStreamRsp(const char* msg);
	~EasyDarwinClientStopStreamRsp() {}
};

class EasyDarwinDeviceUpdateSnapReq : public EasyProtocol
{
public:
	EasyDarwinDeviceUpdateSnapReq(EasyJsonValue &body, size_t cseq = 1);
	EasyDarwinDeviceUpdateSnapReq(const char* msg);
	~EasyDarwinDeviceUpdateSnapReq() {}
};

class EasyDarwinDeviceUpdateSnapRsp : public EasyProtocol
{
public:
	EasyDarwinDeviceUpdateSnapRsp(EasyJsonValue &body, size_t cseq = 1, size_t error = 200);
	EasyDarwinDeviceUpdateSnapRsp(const char* msg);
	~EasyDarwinDeviceUpdateSnapRsp() {}
};

class EasyDarwinExceptionRsp : public EasyProtocol
{
public:
	EasyDarwinExceptionRsp(size_t cseq = 1, size_t error = 400);
	~EasyDarwinExceptionRsp() {}
};


/*
class EASYDARWIN_API EasyDarwinDeviceListAck : public EasyProtocol
{
public:
	EasyDarwinDeviceListAck();
	EasyDarwinDeviceListAck(const char* msg);
	virtual ~EasyDarwinDeviceListAck(){}

public:
	bool AddDevice(EasyDarwinDevice &device);
	int StartGetDevice();
	bool GetNextDevice(EasyDarwinDevice &device);

private:
	list<EasyDarwinDevice> devices;
};

class EASYDARWIN_API EasyDarwinDeviceSnapUpdateReq : public EasyProtocol
{
public:
	EasyDarwinDeviceSnapUpdateReq();
	EasyDarwinDeviceSnapUpdateReq(const char *msg);
	~EasyDarwinDeviceSnapUpdateReq(){}

public:
	void SetImageData(const char* sImageBase64Data, size_t iBase64DataSize);
	bool GetImageData(string &sImageBase64Data);
};

class EASYDARWIN_API EasyDarwinDeviceSnapUpdateAck : public EasyProtocol
{
public:
	EasyDarwinDeviceSnapUpdateAck();
	EasyDarwinDeviceSnapUpdateAck(const char *msg);
	~EasyDarwinDeviceSnapUpdateAck(){}
};
*/
class EasyDarwinHLSession
{
public:
	EasyDarwinHLSession(){}
	~EasyDarwinHLSession(){}

public:
	int index;
	std::string SessionName;
	std::string HlsUrl;
	std::string sourceUrl;
	int bitrate;
};
class EasyDarwinRTSPSession
{
public:
	EasyDarwinRTSPSession(){}
	~EasyDarwinRTSPSession(){}

public:
	int index;
	std::string Url;
	std::string Name;
	int numOutputs;
};


class EASYDARWIN_API EasyDarwinEasyHLSAck : public EasyProtocol
{
public:
	EasyDarwinEasyHLSAck();
	EasyDarwinEasyHLSAck(const char *msg);
	~EasyDarwinEasyHLSAck(){}

	void SetStreamName(const char* sName);
	void SetStreamURL(const char* sURL);
};

class EASYDARWIN_API EasyDarwinHLSessionListAck : public EasyProtocol
{
public:
	EasyDarwinHLSessionListAck();
	EasyDarwinHLSessionListAck(const char* msg);
	virtual ~EasyDarwinHLSessionListAck(){}

public:
	bool AddSession(EasyDarwinHLSession &session);
	//int StartGetDevice();
	//bool GetNextDevice(EasyDarwinDevice &device);

private:
	list<EasyDarwinHLSession> sessions;
};
class EASYDARWIN_API EasyDarwinRTSPPushSessionListAck : public EasyProtocol
{
public:
	EasyDarwinRTSPPushSessionListAck();
	EasyDarwinRTSPPushSessionListAck(const char* msg);
	virtual ~EasyDarwinRTSPPushSessionListAck(){}

public:
	bool AddSession(EasyDarwinRTSPSession &session);
	//int StartGetDevice();
	//bool GetNextDevice(EasyDarwinDevice &device);

private:
	std::list<EasyDarwinRTSPSession> sessions;
};


class EASYDARWIN_API EasyDarwinRecordListAck : public EasyProtocol
{
public:
	EasyDarwinRecordListAck();
	EasyDarwinRecordListAck(const char* msg);
	virtual ~EasyDarwinRecordListAck(){}

public:
	bool AddRecord(std::string record);

private:
	std::list<std::string> records;
};


//add,�Ϲ⣬start
enum EasyDSSTerminalType//�豸����
{
	EASYDSS_TERMINAL_TYPE_CAMERA	= 0,//�����
	EASYDSS_TERMINAL_TYPE_NVR		= 1,//NVR
	EASYDSS_TERMINAL_TYPE_SMARTHOST = 2,//��������
	EASYDSS_TERMINAL_TYPE_NUM		= 3 //�豸���͸���
};
enum EasyDSSAppType//�豸����
{
	EASYDSS_APP_TYPE_ARM_LINUX		= 0,//linux�ն�
	EASYDSS_APP_TYPE_ANDROID		= 1,//Andorid Cli
	EASYDSS_APP_TYPE_IOS			= 2,//IOS Cli
	EASYDSS_APP_TYPE_WEB			= 3,//Web Cli
	EASYDSS_APP_TYPE_PC				= 4,//PC Cli
	EASYDSS_APP_TYPE_NUM			= 5
};
typedef struct 
{
	string strDeviceSerial;//�豸���к�
	string strCameraSerial;//����ͷ���к�
	string strProtocol;//ֱ��Э��
	string strStreamID;//ֱ��������
}stStreamInfo;
typedef set<void*> DevSet;//void*��Ӧ���ǿͻ��˶���ָ�룬�ͻ��˿�ʼֱ��ʱ���룬ֱֹͣ��ʱɾ�����ͻ���������ֹʱɾ��
typedef map<string,DevSet> DevMap;//keyΪstring��ʾ��ǰ�豸�µ�ĳ������ͷ��valueΪ���������ͷ���������Ŀͻ���
typedef DevMap::iterator DevMapItera;

typedef map<string,stStreamInfo> CliStreamMap;//�ͻ��˴洢���е�ֱ����Ϣ��keyΪ�豸���кź�����ͷ���кŵ���ϣ�valueΪ�����ͺ�Э��.�����жϿͻ��˶���Щ�豸������ֱ����һ���ͻ��˿���ͬʱ�Զ���豸�Ķ������ͷͬʱ����ֱ��
typedef CliStreamMap::iterator CliStreamMapItera;
class strDevice//�豸���Ͷ�Ӧ����Ϣ
{
public:
	bool GetDevInfo(const char *msg);//��JSON�ı��õ��豸��Ϣ
public:
	string serial_;//�豸���к�
	string name_;//�豸����
	string password_;//����
	string tag_;//��ǩ
	string channelCount_;//���豸����������ͷ����

	EasyDevices cameras_;//����ͷ��Ϣ
	EasyDSSTerminalType eDeviceType;//�豸����
	EasyDSSAppType eAppType;//App����
};

class EASYDARWIN_API EasyDarwinRSP : public EasyProtocol//��װCMS��һ���Ӧ��JSON����
{
public:
	EasyDarwinRSP(int iMsgType):EasyProtocol(iMsgType){}
	void SetHead(EasyJsonValue& header);//����ͷ��
	void SetBody(EasyJsonValue& body);//����JSON
};
class EASYDARWIN_API EasyDarwinRecordListRSP : public EasyDarwinRSP//��װ¼���б��Ӧ
{
public:
	EasyDarwinRecordListRSP(int iMsgType):EasyDarwinRSP(iMsgType){}
	void AddRecord(std::string record);
};
//add,�Ϲ⣬end
}}//namespace
#endif

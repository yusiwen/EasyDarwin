/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/

#include <EasyProtocol.h>
#include <string.h>
#include <stdio.h>
#include <EasyUtil.h>

namespace EasyDarwin { namespace Protocol
{

EasyDevice::EasyDevice()
{
}

EasyDevice::EasyDevice(std::string channel, std::string name)
: serial_(channel)
, name_(name)
{

}

EasyDevice::EasyDevice(std::string channel, std::string name, std::string status)
: serial_(channel)
, name_(name)
, status_(status)
{
}


EasyNVR::EasyNVR()
{
}

EasyNVR::EasyNVR(std::string serial, std::string name, std::string password, string tag, EasyDevices &channel)
: channels_(channel)
{
	serial_ = serial;	
	name_ = name;
	password_ = password;
	tag_ = tag;
}

// MSG_DS_REGISTER_REQ��Ϣ���Ĺ���
EasyMsgDSRegisterREQ::EasyMsgDSRegisterREQ(EasyDarwinTerminalType terminalType, EasyDarwinAppType appType, EasyNVR &nvr, size_t cseq)
: EasyProtocol(MSG_DS_REGISTER_REQ)
, nvr_(nvr)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("AppType", EasyProtocol::GetAppTypeString(appType));
	SetHeaderValue("TerminalType", EasyProtocol::GetTerminalTypeString(terminalType));

	SetBodyValue("Serial", nvr.serial_);
	SetBodyValue("Name", nvr.name_);
	SetBodyValue("Tag", nvr.tag_);
	SetBodyValue("Token", nvr.password_);
	if(appType == EASY_APP_TYPE_NVR)
	{
		SetBodyValue("ChannelCount", nvr.channels_.size());
		for(EasyDevices::iterator it = nvr.channels_.begin(); it != nvr.channels_.end(); it++)
		{
			Json::Value value;
			value["Channel"] = it->serial_;
			value["Name"] = it->name_;		
			value["Status"] = it->status_;
			root[EASY_TAG_ROOT][EASY_TAG_BODY]["Channels"].append(value);		
		}
	}
}

// MSG_DS_REGISTER_REQ��Ϣ���Ľ���
EasyMsgDSRegisterREQ::EasyMsgDSRegisterREQ(const char* msg)
: EasyProtocol(msg, MSG_DS_REGISTER_REQ)
{
	nvr_.serial_ = GetBodyValue("DeviceSerial");
	nvr_.name_ = GetBodyValue("DeviceName");
	nvr_.tag_ = GetBodyValue("DeviceTag");
	nvr_.password_ = GetBodyValue("AuthCode");
	
	nvr_.channels_.clear();

	int size = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Cameras"].size();  

	for(int i = 0; i < size; i++)  
	{  
		Json::Value &json_camera = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Cameras"][i];  
		EasyDevice camera;
		camera.name_ = json_camera["CameraName"].asString();
		camera.serial_ = json_camera["CameraSerial"].asString();		
		camera.status_ = json_camera["Status"].asString();	
		nvr_.channels_.push_back(camera);
	}  
}

// MSG_SD_REGISTER_ACK��Ϣ����
EasyMsgSDRegisterACK::EasyMsgSDRegisterACK(EasyJsonValue &body, size_t cseq, size_t error)
: EasyProtocol(MSG_SD_REGISTER_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

// MSG_SD_REGISTER_ACK��Ϣ����
EasyMsgSDRegisterACK::EasyMsgSDRegisterACK(const char* msg)
: EasyProtocol(msg, MSG_SD_REGISTER_ACK)
{
}

// MSG_SD_PUSH_STREAM_REQ��Ϣ����
EasyMsgSDPushStreamREQ::EasyMsgSDPushStreamREQ(EasyJsonValue &body, size_t cseq)
: EasyProtocol(MSG_SD_PUSH_STREAM_REQ)
{
	SetHeaderValue("CSeq", cseq);

	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

// MSG_SD_PUSH_STREAM_REQ��Ϣ����
EasyMsgSDPushStreamREQ::EasyMsgSDPushStreamREQ(const char *msg)
: EasyProtocol(msg, MSG_SD_PUSH_STREAM_REQ)
{
}

// MSG_DS_PUSH_STREAM_ACK��Ϣ����
EasyMsgDSPushSteamACK::EasyMsgDSPushSteamACK(EasyJsonValue &body, size_t cseq, size_t error)
: EasyProtocol(MSG_DS_PUSH_STREAM_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

// MSG_DS_PUSH_STREAM_ACK��Ϣ����
EasyMsgDSPushSteamACK::EasyMsgDSPushSteamACK(const char *msg)
: EasyProtocol(msg, MSG_DS_PUSH_STREAM_ACK)
{
}

// MSG_SD_STREAM_STOP_REQ��Ϣ����
EasyMsgSDStopStreamREQ::EasyMsgSDStopStreamREQ(EasyJsonValue &body, size_t cseq)
:EasyProtocol(MSG_SD_STREAM_STOP_REQ)
{
	SetHeaderValue("CSeq", cseq);
	
	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

// MSG_SD_STREAM_STOP_REQ��Ϣ����
EasyMsgSDStopStreamREQ::EasyMsgSDStopStreamREQ(const char *msg)
: EasyProtocol(msg, MSG_SD_STREAM_STOP_REQ)
{
}

// MSG_SD_STREAM_STOP_REQ��Ϣ����
EasyMsgDSStopStreamACK::EasyMsgDSStopStreamACK(EasyJsonValue &body, size_t cseq, size_t error)
: EasyProtocol(MSG_SD_STREAM_STOP_REQ)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

// MSG_SD_STREAM_STOP_REQ��Ϣ����
EasyMsgDSStopStreamACK::EasyMsgDSStopStreamACK(const char *msg)
: EasyProtocol(msg, MSG_SD_STREAM_STOP_REQ)
{
}

/*
EasyDarwinDeviceListAck::EasyDarwinDeviceListAck()
: EasyProtocol(MSG_CLI_CMS_DEVICE_LIST_ACK)
{
}

EasyDarwinDeviceListAck::EasyDarwinDeviceListAck(const char* msg)
: EasyProtocol(msg, MSG_CLI_CMS_DEVICE_LIST_ACK)
{
}

bool EasyDarwinDeviceListAck::AddDevice(EasyDarwinDevice &device)
{	
	Json::Value value;
	value["DeviceSerial"] = device.DeviceSerial;
	value["DeviceName"] = device.DeviceName;
	value["DeviceSnap"] = device.DeviceSnap;
	root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"].append(value);
	return true;
}

int EasyDarwinDeviceListAck::StartGetDevice()
{
	devices.clear();	
	
	int size = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"].size();  

	for(int i = 0; i < size; i++)  
	{  
		Json::Value &json_device = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"][i];  
		EasyDarwinDevice device;
		device.DeviceName = json_device["DeviceName"].asString();
		device.DeviceSerial = json_device["DeviceSerial"].asString();    
		device.DeviceSnap = json_device["DeviceSnap"].asString();

		devices.push_back(device);
	}  
	
	return devices.size();	
}

bool EasyDarwinDeviceListAck::GetNextDevice(EasyDarwinDevice &device)
{
	if(devices.empty())
    {
        return false;
    }
    
    device = devices.front();
    devices.pop_front();   

    return true;
}

EasyDarwinDeviceSnapUpdateReq::EasyDarwinDeviceSnapUpdateReq()
: EasyProtocol(MSG_DEV_CMS_SNAP_UPDATE_REQ)
{
}

EasyDarwinDeviceSnapUpdateReq::EasyDarwinDeviceSnapUpdateReq(const char *msg)
: EasyProtocol(msg, MSG_DEV_CMS_SNAP_UPDATE_REQ)
{
}


void EasyDarwinDeviceSnapUpdateReq::SetImageData(const char* sImageBase64Data, size_t iBase64DataSize)
{
	//std::string data;
	//data.assign(sImageBase64Data, iBase64DataSize);
	SetBodyValue("Img", sImageBase64Data);
}

bool EasyDarwinDeviceSnapUpdateReq::GetImageData(std::string &sImageBase64Data)
{
	sImageBase64Data.clear();
	sImageBase64Data = GetBodyValue("Img");
	return !sImageBase64Data.empty();
}

EasyDarwinDeviceSnapUpdateAck::EasyDarwinDeviceSnapUpdateAck()
: EasyProtocol(MSG_DEV_CMS_SNAP_UPDATE_ACK)
{
}

EasyDarwinDeviceSnapUpdateAck::EasyDarwinDeviceSnapUpdateAck(const char *msg)
: EasyProtocol(msg, MSG_DEV_CMS_SNAP_UPDATE_ACK)
{
}
*/

EasyDarwinEasyHLSAck::EasyDarwinEasyHLSAck()
: EasyProtocol(MSG_CLI_SMS_HLS_ACK)
{
}

EasyDarwinEasyHLSAck::EasyDarwinEasyHLSAck(const char *msg)
: EasyProtocol(msg, MSG_CLI_SMS_HLS_ACK)
{
}

void EasyDarwinEasyHLSAck::SetStreamName(const char* sName)
{
	SetBodyValue("name", sName);
}

void EasyDarwinEasyHLSAck::SetStreamURL(const char* sURL)
{
	SetBodyValue("url", sURL);
}

EasyDarwinHLSessionListAck::EasyDarwinHLSessionListAck()
: EasyProtocol(MSG_CLI_SMS_HLS_LIST_ACK)
{
}

EasyDarwinHLSessionListAck::EasyDarwinHLSessionListAck(const char* msg)
: EasyProtocol(msg, MSG_CLI_SMS_HLS_LIST_ACK)
{
}

bool EasyDarwinHLSessionListAck::AddSession(EasyDarwinHLSession &session)
{	
	Json::Value value;
	value["index"] = session.index;
	value["name"] = session.SessionName;
	value["url"] = session.HlsUrl;
	value["source"] = session.sourceUrl;
	value["Bitrate"] = session.bitrate;
	root[EASY_TAG_ROOT][EASY_TAG_BODY]["Sessions"].append(value);
	return true;
}


EasyMsgSCDeviceListACK::EasyMsgSCDeviceListACK(EasyDevices & devices, size_t cseq, size_t error)
: EasyProtocol(MSG_SC_DEVICE_LIST_ACK)
, devices_(devices)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	SetBodyValue("DeviceCount", devices.size());
	for (EasyDevices::iterator it = devices.begin(); it != devices.end(); it++)
	{
		Json::Value value;
		value["DeviceSerial"] = it->serial_;
		value["DeviceName"] = it->name_;
		value["DeviceTag"] = it->tag_;
		//value["Status"] = it->status_;
		root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"].append(value);
	}
}

EasyMsgSCDeviceListACK::EasyMsgSCDeviceListACK(const char * msg)
: EasyProtocol(msg, MSG_SC_DEVICE_LIST_ACK)
{
	devices_.clear();
	int size = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"].size();

	for (int i = 0; i < size; i++)
	{
		Json::Value &json_ = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Devices"][i];
		EasyDevice device;
		device.name_ = json_["DeviceSerial"].asString();
		device.serial_ = json_["DeviceName"].asString();
		device.tag_ = json_["DeviceTag"].asString();
		//device.status_ = json_["Status"].asString();
		devices_.push_back(device);
	}
}


EasyMsgSCDeviceInfoACK::EasyMsgSCDeviceInfoACK(EasyDevices & cameras, string devcei_serial, size_t cseq, size_t error)
: EasyProtocol(MSG_SC_CAMERA_LIST_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	SetBodyValue("DeviceSerial", devcei_serial);
	SetBodyValue("CameraCount", cameras.size());
	for (EasyDevices::iterator it = cameras.begin(); it != cameras.end(); it++)
	{
		Json::Value value;
		value["CameraSerial"] = it->serial_;
		value["CameraName"] = it->name_;
        value["Status"] = it->status_;
		root[EASY_TAG_ROOT][EASY_TAG_BODY]["Cameras"].append(value);
	}
}

EasyMsgSCDeviceInfoACK::EasyMsgSCDeviceInfoACK(const char * msg)
: EasyProtocol(msg, MSG_SC_CAMERA_LIST_ACK)
{
	cameras_.clear();
	int size = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Cameras"].size();

	for (int i = 0; i < size; i++)
	{
		Json::Value &json_ = root[EASY_TAG_ROOT][EASY_TAG_BODY]["Cameras"][i];
		EasyDevice camera;
		camera.name_ = json_["CameraSerial"].asString();
		camera.serial_ = json_["CameraName"].asString();
        camera.status_ = json_["Status"].asString();
		cameras_.push_back(camera);
	}
}

EasyMsgSCGetStreamACK::EasyMsgSCGetStreamACK(EasyJsonValue &body, size_t cseq, size_t error)
: EasyProtocol(MSG_SC_GET_STREAM_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for (EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

EasyMsgSCGetStreamACK::EasyMsgSCGetStreamACK(const char *msg)
: EasyProtocol(msg, MSG_SC_GET_STREAM_ACK)
{
}

EasyMsgSCFreeStreamACK::EasyMsgSCFreeStreamACK(EasyJsonValue & body, size_t cseq, size_t error)
: EasyProtocol(MSG_SC_FREE_STREAM_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for (EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

EasyMsgSCFreeStreamACK::EasyMsgSCFreeStreamACK(const char * msg)
: EasyProtocol(msg, MSG_SC_FREE_STREAM_ACK)
{
}

EasyMsgDSPostSnapREQ::EasyMsgDSPostSnapREQ(EasyJsonValue & body, size_t cseq)
: EasyProtocol(MSG_DS_POST_SNAP_REQ)
{
	SetHeaderValue("CSeq", cseq);

	for (EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

EasyMsgDSPostSnapREQ::EasyMsgDSPostSnapREQ(const char * msg)
: EasyProtocol(msg, MSG_DS_POST_SNAP_REQ)
{
}

EasyMsgSDPostSnapACK::EasyMsgSDPostSnapACK(EasyJsonValue & body, size_t cseq, size_t error)
: EasyProtocol(MSG_SD_POST_SNAP_ACK)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));

	for (EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}

EasyMsgSDPostSnapACK::EasyMsgSDPostSnapACK(const char * msg)
: EasyProtocol(msg, MSG_SD_POST_SNAP_ACK)
{
}

EasyMsgExceptionACK::EasyMsgExceptionACK(size_t cseq, size_t error)
:EasyProtocol(MSG_SC_EXCEPTION)
{
	SetHeaderValue("CSeq", cseq);
	SetHeaderValue("ErrorNum", error);
	SetHeaderValue("ErrorString", GetErrorString(error));
}

EasyDarwinRTSPPushSessionListAck::EasyDarwinRTSPPushSessionListAck()
: EasyProtocol(MSG_CLI_SMS_PUSH_SESSION_LIST_ACK)
{
}

EasyDarwinRTSPPushSessionListAck::EasyDarwinRTSPPushSessionListAck(const char* msg)
: EasyProtocol(msg, MSG_CLI_SMS_PUSH_SESSION_LIST_ACK)
{
}
bool EasyDarwinRTSPPushSessionListAck::AddSession(EasyDarwinRTSPSession &session)
{	
	Json::Value value;
	value["index"] = session.index;
	value["url"] = session.Url;
	value["name"] = session.Name;
	root[EASY_TAG_ROOT][EASY_TAG_BODY]["Sessions"].append(value);
	return true;
}

EasyDarwinRecordListAck::EasyDarwinRecordListAck()
: EasyProtocol(MSG_SC_LIST_RECORD_ACK)
{
}

EasyDarwinRecordListAck::EasyDarwinRecordListAck(const char *msg)
: EasyProtocol(msg, MSG_SC_LIST_RECORD_ACK)
{
}

bool EasyDarwinRecordListAck::AddRecord(std::string record)
{
	Json::Value value;	
	value["url"] = record;	
	int pos = record.find_last_of('/');	
	value["time"] = record.substr(pos - 14, 14); // /20151123114500/*.m3u8
	root[EASY_TAG_ROOT][EASY_TAG_BODY]["Records"].append(value);
	return true;
}

//add,�Ϲ⣬start
string TerminalTypeMap[] ={"EasyCamera","EasyNVR"};
string AppType[]={"ARM_Linux","Android","IOS","WEB","PC"};
bool strDevice::GetDevInfo(const char* json)//��JSON�ı��õ��豸��Ϣ
{
	EasyProtocol proTemp(json);
	do 
	{
		string strTerminalType	=	proTemp.GetHeaderValue("TerminalType");//��ȡ�豸����
		string strAppType		=	proTemp.GetHeaderValue("AppType");//��ȡApp����
		serial_					=	proTemp.GetBodyValue("Serial");//��ȡ�豸���к�

		if(strTerminalType.size()<=0||serial_.size()<=0||strAppType.size()<=0)
			break;
		
		int i=0;
		for(;i<EASYDSS_TERMINAL_TYPE_NUM;i++)//��ȡ�豸����
		{
			if(strTerminalType==TerminalTypeMap[i])
			{
				eDeviceType=(EasyDSSTerminalType)i;
				break;
			}
		}
		if(i>=EASYDSS_TERMINAL_TYPE_NUM)
			break;
		for(i=0;i<EASYDSS_APP_TYPE_NUM;i++)//��ȡApp����
		{
			if(strAppType==AppType[i])
			{
				eAppType=(EasyDSSAppType)i;
				break;
			}
		}
		if(i>=EASYDSS_APP_TYPE_NUM)
			break;

		name_			=	proTemp.GetBodyValue("Name");//��ȡ�豸����
		password_		=	proTemp.GetBodyValue("Token");//�豸��֤��
		tag_			=	proTemp.GetBodyValue("Tag");//��ǩ
		channelCount_	=	proTemp.GetBodyValue("ChannelCount");//��ǰ�豸����������ͷ����

		if(eDeviceType==EASYDSS_TERMINAL_TYPE_NVR)//����豸������NVR������Ҫ��ȡ����ͷ��Ϣ
		{
			cameras_.clear();
			Json::Value *proot=proTemp.GetRoot();
			int size = (*proot)[EASY_TAG_ROOT][EASY_TAG_BODY]["Channels"].size(); //�����С 

			for(int i = 0; i < size; i++)  
			{  
				Json::Value &json_camera = (*proot)[EASY_TAG_ROOT][EASY_TAG_BODY]["Channels"][i];  
				EasyDevice camera;
				camera.name_ = json_camera["Name"].asString();
				camera.channel_ = json_camera["Channel"].asString();		
				camera.status_ = json_camera["Status"].asString();	
				cameras_.push_back(camera);
			}  
		}
		return true;
	} while (0);
	//ִ�е���˵���õ����豸��Ϣ�Ǵ����
	return false;
}

void EasyDarwinRSP::SetHead(EasyJsonValue &header)
{
	for(EasyJsonValue::iterator it = header.begin(); it != header.end(); it++)
	{
		SetHeaderValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}
void EasyDarwinRSP::SetBody(EasyJsonValue &body)
{
	for(EasyJsonValue::iterator it = body.begin(); it != body.end(); it++)
	{
		SetBodyValue(it->first.c_str(), boost::apply_visitor(EasyJsonValueVisitor(), it->second));
	}
}
void EasyDarwinRecordListRSP::AddRecord(std::string record)
{
	Json::Value value;	
	value["url"] = record;	
	int pos = record.find_last_of('/');	
	value["time"] = record.substr(pos - 14, 14); // /20151123114500/*.m3u8
	root[EASY_TAG_ROOT][EASY_TAG_BODY]["Records"].append(value);
}
//add,�Ϲ⣬end
}
}//namespace


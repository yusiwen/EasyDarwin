/*
	Copyright (c) 2013-2015 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
#ifndef _Easy_CMS_API_H
#define _Easy_CMS_API_H

#ifdef _WIN32
#define EasyCMS_API  __declspec(dllexport)
#define Easy_APICALL  __stdcall
#else
#define EasyCMS_API
#define Easy_APICALL 
#endif

#define Easy_CMS_Handle void*

typedef unsigned char           Easy_U8;
typedef unsigned char           Easy_UCHAR;
typedef unsigned short          Easy_U16;
typedef unsigned int            Easy_U32;

enum
{
    Easy_NoErr              = 0,
    Easy_RequestFailed      = -1,
    Easy_Unimplemented      = -2,
    Easy_RequestArrived     = -3,
    Easy_OutOfState         = -4,
    Easy_NotAModule         = -5,
    Easy_WrongVersion       = -6,
    Easy_IllegalService     = -7,
    Easy_BadIndex           = -8,
    Easy_ValueNotFound      = -9,
    Easy_BadArgument        = -10,
    Easy_ReadOnly           = -11,
    Easy_NotPreemptiveSafe  = -12,
    Easy_NotEnoughSpace     = -13,
    Easy_WouldBlock         = -14,
    Easy_NotConnected       = -15,
    Easy_FileNotFound       = -16,
    Easy_NoMoreData         = -17,
    Easy_AttrDoesntExist    = -18,
    Easy_AttrNameExists     = -19,
    Easy_InstanceAttrsNotAllowed= -20,
	Easy_InvalidSocket		= -21,
	Easy_MallocError		= -22,
	Easy_ConnectError		= -23,
	Easy_SendError			= -24
};
typedef int Easy_Error;

/* CMS�¼����Ͷ��� */
typedef enum __EASY_CMS_EVENT_T
{
    Easy_CMS_Event_Login   =   1,		/* ���ӳɹ� */
    Easy_CMS_Event_Offline,             /* ���� */

}EASY_CMS_EVENT_T;

/* ���ͻص��������� _userptr��ʾ�û��Զ������� */
typedef int (*EasyCMS_Callback)(EASY_CMS_EVENT_T _event, const char* _pEventData, void* _pUserData);

#ifdef __cplusplus
extern "C"
{
#endif

	/* ����CMS Session  ����Ϊ���ֵ */
	EasyCMS_API Easy_CMS_Handle Easy_APICALL EasyCMS_Session_Create();
	
	/* �ͷ�CMS Session��� */
	EasyCMS_API Easy_U32 Easy_APICALL EasyCMS_Session_Release(Easy_CMS_Handle handle);

    /* �����������¼��ص� userptr�����Զ������ָ��*/
    EasyCMS_API Easy_U32 Easy_APICALL EasyCMS_SetEventCallback(Easy_CMS_Handle handle,  EasyCMS_Callback callback, void *userData);

	/* ��¼��CMS��Я��������û������� */
	EasyCMS_API Easy_U32 Easy_APICALL EasyCMS_Login(Easy_CMS_Handle handle, char* serverAddr, Easy_U16 port, char *username, char *password);

	/* �ϴ����� */
	EasyCMS_API Easy_U32 Easy_APICALL EasyCMS_UpdateSnap(Easy_CMS_Handle handle, const char* snapData, unsigned int snapLen);

#ifdef __cplusplus
}
#endif


#endif
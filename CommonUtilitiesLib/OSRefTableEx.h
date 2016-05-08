
#ifndef _OSREFTABLEEX_H_
#define _OSREFTABLEEX_H_
//��Ϊdarwin�����ñ�������ͬ��key���������ַ���hash�����ֲ���������һ�㡣��˿���ʹ��STL�е�map���д��档
//ע��������Ǻ������ϵģ�����Darwin�Դ���
#include<map>
#include<string>
using namespace std;

#include "OSCond.h"
#include "OSHeaders.h"
class OSRefTableEx
{
public:
	class OSRefEx
	{
	private:
		void *mp_Object;//���õĶ������������
		int m_Count;//��ǰ���ö��������ֻ��Ϊ0ʱ�������������
		OSCond  m_Cond;//to block threads waiting for this ref.
	public:
		OSRefEx():mp_Object(NULL),m_Count(0){}
		OSRefEx(void * pobject):mp_Object(pobject),m_Count(0){}
	public:
		void *GetObjectPtr(){return mp_Object;}
		int GetRefNum(){return m_Count;}
		OSCond *GetCondPtr(){return &m_Cond;}
		void AddRef(int num){m_Count+=num;}
	};
private:
	map<string,OSRefTableEx::OSRefEx*> m_Map;//��stringΪkey����OSRefExΪvalue
	OSMutex         m_Mutex;//�ṩ��map�Ļ������
public:
	OSRefEx *    Resolve(string keystring);//���ö���
	OS_Error     Release(string keystring);//�ͷ�����
	OS_Error     Register(string keystring,void* pobject);//���뵽map��
	OS_Error     UnRegister(string keystring);//��map���Ƴ�

	OS_Error TryUnRegister(string keystring);//�����Ƴ����������Ϊ0,���Ƴ�������true�����򷵻�false
public:
	int GetEleNumInMap(){return m_Map.size();}//���map�е�Ԫ�ظ���
	OSMutex *GetMutex(){return &m_Mutex;}//�������ṩ����ӿ�
	map<string,OSRefTableEx::OSRefEx*> *GetMap(){return &m_Map;}
};
typedef map<string,OSRefTableEx::OSRefEx*> OSHashMap;
typedef map<string,OSRefTableEx::OSRefEx*>::iterator OSRefIt;
#endif _OSREFTABLEEX_H_
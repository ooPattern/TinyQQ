/*
 * connectPool.cc
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */
#include <stdio.h>
#include <string.h>
#include "connectPool.h"

using namespace std;

//初始化连接池句柄指针
CConnectPool* CConnectPool::_pInstance = NULL;

//构造函数,初始化连接池
CConnectPool::CConnectPool()
{
	T_CONNECT_OBJ obj;

	//分配连接池的内存和初始化连接标志
	_connPool.clear();
	for(int i = 0; i < MAX_CONNECT_NUM; i++)
	{
		obj._valid = false;
		obj._pConnect = mysql_init(NULL);
		_connPool.push_back(obj);
	}
}

//析构函数
CConnectPool::~CConnectPool()
{
	DestoryConnectPool();
}

//获取连接池对象指针
CConnectPool* CConnectPool::GetInstance(void)
{
	if(NULL == _pInstance)
	{
		_pInstance = new CConnectPool();
	}
	return _pInstance;
}

//匹配连接信息
int CConnectPool::MatchLogin(const MYSQL* pLogin,
				   	   	     const char* pHost,
				   	   	     const char* pUser,
				   	   	     const char* pDbname)
{
	if((strcmp(pLogin->host, pHost) != 0)
	|| (strcmp(pLogin->user, pUser) != 0)
	|| (strcmp(pLogin->db, pDbname) != 0))
	{
		return -1;
	}
	return 0;
}

//从连接池中获取一个连接
MYSQL* CConnectPool::GetConnection(const char* pHost,
			  	  	  	 	 	   const short port,
			  	  	  	 	 	   const char* pUser,
			  	  	  	 	 	   const char* pPassword,
			  	  	  	 	 	   const char* pDbname)
{
	MYSQL* pSQL = NULL;//初始值必须为空

	//连接池为空表示没有可用连接,应用程序要不等待一段时间再尝试吧
	if(_connPool.empty())
	{
		printf("no idle connection err\n");
		return NULL;
	}

	//遍历连接池检查是否存在可用的连接
	list<T_CONNECT_OBJ>::iterator it;
	for(it = _connPool.begin(); it != _connPool.end(); ++it)
	{
		//连接池上以前使用过相同信息的连接
		if((true == (*it)._valid)
		&& (0 == MatchLogin((*it)._pConnect, pHost, pUser, pDbname)))
		{
			pSQL = (*it)._pConnect;
			//从连接池移除这个元素
			it = _connPool.erase(it);
			return pSQL;
		}
	}

	//如果没有存在可以直接使用的连接,就检查是否存在没有初始化的连接
	for(it = _connPool.begin(); it != _connPool.end(); ++it)
	{
		if(false == (*it)._valid)
		{
			//连接池上以前没有使用过相同信息的连接
			if(NULL == mysql_real_connect((*it)._pConnect, pHost, pUser,
						pPassword, pDbname, port, NULL, 0))
			{
				printf("mysql_real_connect err\n");
				return NULL;
			}
			//设置连接成功状态
			pSQL = (*it)._pConnect;
			//从连接池移除这个元素
			it = _connPool.erase(it);
			return pSQL;
		}
	}

	//连接池已满,不能接受新的连接
	return NULL;
}

//回收一个连接到连接池
void CConnectPool::RecoverConnection(MYSQL* pConnect)
{
	T_CONNECT_OBJ obj;
	//正常情况不会出现这种情况,总共分配的连接池怎么回收数量比分配的还多?
	if(_connPool.size() > MAX_CONNECT_NUM)
	{
		printf("RecoverConnection full err\n");
		return;
	}
	obj._valid = true;
	obj._pConnect = pConnect;
	_connPool.push_back(obj);
}

//销毁连接池
void CConnectPool::DestoryConnectPool(void)
{
	MYSQL* pSQL = NULL;
	list<T_CONNECT_OBJ>::iterator it;

	//释放连接分配的内存
	for(it = _connPool.begin(); it != _connPool.end(); ++it)
	{
		pSQL = (*it)._pConnect;
		(*it)._valid = false;
		if(pSQL != NULL)
		{
			mysql_close(pSQL);
		}
	}
	_connPool.clear();
}




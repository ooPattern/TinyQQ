/*
 * connectPool.h
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */

#ifndef CONNECTPOOL_H_
#define CONNECTPOOL_H_

#include <list>
#include <mysql/mysql.h>

using namespace std;

const int MAX_CONNECT_NUM = 10;

//连接池类
class CConnectPool {
	//连接对象信息
	typedef struct {
		bool _valid;//连接是否有效,true:有效,false:无效
		MYSQL* _pConnect;//连接对象指针
	} T_CONNECT_OBJ;

public:
	//析构函数
	~CConnectPool();
	//获取连接池对象指针
	static CConnectPool* GetInstance(void);
	//从连接池中获取一个连接
	MYSQL* GetConnection(const char* pHost,
			  	  	  	 const short port,
			  	  	  	 const char* pUser,
			  	  	  	 const char* pPassword,
			  	  	  	 const char* pDbname);
	//回收一个连接到连接池
	void RecoverConnection(MYSQL* pConnect);
	//销毁连接池
	void DestoryConnectPool(void);

private:
	list<T_CONNECT_OBJ> _connPool;
	static CConnectPool* _pInstance;

	//构造函数,在private中声明防止其他程序new,因为连接池只允许一个对象
	CConnectPool();
	//匹配连接信息
	int MatchLogin(const MYSQL* pLogin,
				   const char* pHost,
				   const char* pUser,
				   const char* pDbname);
};

#endif /* CONNECTPOOL_H_ */
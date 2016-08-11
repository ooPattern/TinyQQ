/*
 * connectPool.h
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */

#ifndef CONNECTPOOL_H_
#define CONNECTPOOL_H_

#include <vector>
#include <list>
#include <pthread.h>
#include <mysql/mysql.h>

//没有使用智能指针
//#include <tr1/memory>
//using std::tr1::shared_ptr;

using std::vector;
using std::list;

//连接池类
class CConnectPool {
	const int MAX_CONNECT_NUM = 10;

	//连接状态
	typedef enum {
		CONN_INVALID = 0,//连接无效,不能使用
		CONN_IDLE = 1,//连接空闲,可以使用
		CONN_BUSY = 2,//连接正在使用,使用完成后可以回收
	} E_CONN_STAT;
	//连接对象信息
	typedef struct {
		E_CONN_STAT _stat;//连接状态
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
	//多个线程有可能同时访问连接池,因此需要互斥锁同步
	//在任务队列中,同时加入多个数据库查询任务,多个线程可能同时访问线程池第一个可用的连接
	//因此在连接数据库过程中必须通过互斥量进行同步,数据库连接完成了,同步的任务也完成了
	pthread_mutex_t _lock;

	//固定分配,根据构造函数初始化确定连接池的连接数量
	vector<T_CONNECT_OBJ> _connPool;

	//动态分配,如果连接池的连接数不够用,只能通过系统动态分配新的连接
	//虽然连接频繁创建和断开会消耗系统资源,而且造成大量TIME_WAIT,但是总比不能使用好
	list<T_CONNECT_OBJ> _exConnList;

	static CConnectPool* _pInstance;

	//构造函数,在private中声明防止其他程序new,因为连接池只允许一个对象
	CConnectPool();
	//匹配连接信息
	int MatchLogin(const MYSQL* pLogin,
				   const char* pHost,
				   const char* pUser,
				   const char* pDbname);

	//no copy
	CConnectPool(const CConnectPool&);
	CConnectPool& operator=(const CConnectPool);
};

#endif /* CONNECTPOOL_H_ */

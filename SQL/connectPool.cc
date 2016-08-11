/*
 * connectPool.cc
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */
#include <stdio.h>
#include <string.h>
#include "connectPool.h"

/*
 * 情况1:
 * 如果连接池不够用(并发连接太多),系统会尝试重新分配连接,但是这些连接是动态分配的,
 * 通过list维护,如果使用完毕这个连接,会主动回收这个连接的资源,这样在大量连接的情况下,
 * 可能会消耗系统的资源,虽然如此,但起码这个功能是完整的,不至于没有连接可用.
 *
 * 情况2:
 * 如果数据库很小(只有几条记录),就算再多的连接也不会导致大量的TIME_WAIT???
 *
 * 情况3:
 * 如果连接池支持的连接数量和线程支持的数量一致(例如都是10),这时任务队列假设都是数据库查询,
 * 就算有1000个任务,也不会造成连接池连接不够用,因为线程数支持同时处理10个任务,刚好这10个任务可以
 * 满足10个数据库连接,这时不需要额外分配数据库连接
 *
 * 情况4:
 * 如果连接池支持的连接数量比线程数量少,表示因此最多处理3个连接的任务,但是实际上有10个任务在同时运行,
 * 这样会导致连接池的连接不够用,这时需要额外分配数据库的连接才能满足查询的需求
 *
 * 情况5:
 * 线程池中任务队列的任务不能是耗时长的,因为这样会导致任务队列剩下待执行的任务无法执行,
 * 所以像数据库查询相关的任务,最好建立索引优化,否则查询慢的会影响队列任务的执行
 */

//初始化连接池句柄指针
CConnectPool* CConnectPool::_pInstance = NULL;

//构造函数,初始化连接池
CConnectPool::CConnectPool()
{
	int err = 0;
	T_CONNECT_OBJ obj;

	//初始化互斥锁
	err = pthread_mutex_init(&_lock, NULL);
	if(err != 0)
	{
		printf("pthread_mutex_init faid err=%d\n", err);
	}

	//分配连接池的内存和初始化连接标志
	_connPool.clear();
	for(int i = 0; i < MAX_CONNECT_NUM; i++)
	{
		obj._stat = CONN_INVALID;
		obj._pConnect = mysql_init(NULL);
		_connPool.push_back(obj);
	}

	//连接池的连接不够用时,才需要连接链表,会动态分配和释放
	_exConnList.clear();
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
	bool mark = false;//初始值必须为false

	//操作数据库连接时必须采用互斥量,避免多个线程同时操作同一个线程池的连接
	//连接数据库过程中上锁,不知道粒度是不是太大了,会不会延时过长啊
	pthread_mutex_lock(&_lock);
	pSQL = NULL;
	vector<T_CONNECT_OBJ>::iterator it;

	if(mark != true)
	{
		//遍历连接池检查是否存在可用的连接
		for(it = _connPool.begin(); it != _connPool.end(); ++it)
		{
			//连接池上以前使用过相同信息的连接
			if((CONN_IDLE == (*it)._stat)
			&& (0 == MatchLogin((*it)._pConnect, pHost, pUser, pDbname)))
			{
				//设置标志
				mark = true;
				//将连接状态修改为正在使用,不是采用列表的方式移除数据,
				//因为移除数据后如果进程崩溃可能无法回收内存,因此修改为统一由vector回收
				pSQL = (*it)._pConnect;
				(*it)._stat = CONN_BUSY;
				break;
			}
		}
	}

	if(mark != true)
	{
		//如果没有存在可以直接使用的连接,就检查是否存在没有初始化的连接
		for(it = _connPool.begin(); it != _connPool.end(); ++it)
		{
			if(CONN_INVALID == (*it)._stat)
			{
				//设置标志
				mark = true;
				//连接池上以前没有使用过相同信息的连接
				//同一个用户名并且同一个数据库名称不能支持多个连接吗,不可能啊
				//解决: 发现多个线程同时对连接池的同一个连接进行连接,这样导致第一个连接数据成功,
				//但是另外一个连接失败了,问题在于多个线程访问的是同一个连接
				if(NULL == mysql_real_connect((*it)._pConnect, pHost, pUser,
							pPassword, pDbname, port, NULL, 0))
				{
					//连接信息非法吧:用户/数据库名称/密码等错误?
					printf("GetConnection: mysql_real_connect addr=%p err\n",
							(*it)._pConnect);
					break;
				}
				//将连接状态修改为正在使用,不是采用列表的方式移除数据,
				//因为移除数据后如果进程崩溃可能无法回收内存,因此修改为统一由vector回收
				//%p表示指针,如果用%d或者%x会出现编译警告
				pSQL = (*it)._pConnect;
				(*it)._stat = CONN_BUSY;
				//printf("GetConnection: mysql_real_connect addr=%p OK\n", pSQL);
				break;
			}
		}
	}

	if(mark != true)
	{
		//连接池已满,直接动态分配一个连接
		T_CONNECT_OBJ obj;
		pSQL = mysql_init(NULL);
		if(mysql_real_connect(pSQL, pHost, pUser,
					pPassword, pDbname, port, NULL, 0) != NULL)
		{
			//设置标志
			mark = true;
			obj._pConnect = pSQL;
			obj._stat = CONN_BUSY;
			_exConnList.push_back(obj);
			//printf("[extra]GetConnection: mysql_real_connect addr=%p OK\n", pSQL);
		}
		else
		{
			//连接没有成功,释放内存,因为这个没有纳入_exConnList管理,必须手动清理
			mysql_close(pSQL);
		}
	}

	//连接错误(用户名/密码/数据库名称等不匹配)或者系统无法再分配更多连接
	if(NULL == pSQL)
	{
		printf("GetConnection: why no connection? It is impossible...\n");
	}

	//记得解锁,加锁和解锁必须配对使用
	pthread_mutex_unlock(&_lock);

	return pSQL;
}

//回收一个连接到连接池
void CConnectPool::RecoverConnection(MYSQL* pConnect)
{
	bool mark = false;//初始值必须为false

	//更新数据库连接状态也需要加锁,只要是操作数据库连接都要加锁
	//由于使用完数据库连接时需要频繁更改连接状态,因此没必要采用读写锁方式
	pthread_mutex_lock(&_lock);

	if(mark != true)
	{
		//在连接池中检查是否找到这个连接
		vector<T_CONNECT_OBJ>::iterator it;
		for(it = _connPool.begin(); it != _connPool.end(); it++)
		{
			//将连接修改为空闲状态,这样下次才可以继续使用
			if((pConnect != NULL)
			&& ((*it)._pConnect == pConnect)
			&& ((*it)._stat != CONN_INVALID))
			{
				mark = true;
				(*it)._stat = CONN_IDLE;
				break;
			}
		}
	}

	if(mark != true)
	{
		//连接池找不到这个连接,有可能该连接在动态分配的连接链表中,因为当时连接池的连接不够用
		list<T_CONNECT_OBJ>::iterator exIt;
		for(exIt = _exConnList.begin(); exIt != _exConnList.end(); ++exIt)
		{
			//直接释放这个连接,下次要用重新动态分配
			if((pConnect != NULL)
			&& ((*exIt)._pConnect == pConnect)
			&& ((*exIt)._stat != CONN_INVALID))
			{
				mark = true;
				mysql_close(pConnect);
				exIt = _exConnList.erase(exIt);
				break;
			}
		}
	}

	//记得解锁,加锁和解锁必须配套使用
	pthread_mutex_unlock(&_lock);

	//正常情况不会打印下列语句,连接池和动态连接链表中都找不到要回收的连接?
	if(false == mark)
	{
		printf("RecoverConnection addr=%p can not find err\n", pConnect);
	}
}

//销毁连接池,如果忘记调用怎么办?
void CConnectPool::DestoryConnectPool(void)
{
	MYSQL* pSQL = NULL;
	vector<T_CONNECT_OBJ>::iterator it;

	//释放连接分配的内存
	for(it = _connPool.begin(); it != _connPool.end(); ++it)
	{
		pSQL = (*it)._pConnect;
		(*it)._stat = CONN_INVALID;
		if(pSQL != NULL)
		{
			mysql_close(pSQL);
		}
	}
	_connPool.clear();

	//如果还有连接没有回收,在析构函数中必须回收这些连接
	list<T_CONNECT_OBJ>::iterator exIt;
	for(exIt = _exConnList.begin(); exIt != _exConnList.end(); ++exIt)
	{
		//直接释放这个连接,下次要用重新动态分配
		if((*it)._pConnect != NULL)
		{
			mysql_close((*it)._pConnect);
			exIt = _exConnList.erase(exIt);
		}
	}
	_exConnList.clear();

	//销毁互斥锁
	pthread_mutex_destroy(&_lock);

}




/*
 * mysqlIntf.cc
 *
 *  Created on: Jul 31, 2016
 *      Author: root
 */
#include <stdio.h>
#include <string.h>
#include "mysqlIntf.h"
#include "connectPool.h"

//查询语句
#define QUERY_LIDI_RECORD ("SELECT * FROM pupil WHERE name='lidi';")

//为什么命令行登陆mysql -u root在netstat命令上看不到连接信息:netstat -na | grep 3306
//但是用程序执行mysql_real_connect就可以通过netstat看到连接信息？难道命令行没有连接到服务器吗?
//但是命名行用show processlist;却可以看到有多个用户登陆服务器的信息?
//mysql> show processlist;
//+----+------+-----------------+---------+---------+------+-------+------------------+
//| Id | User | Host            | db      | Command | Time | State | Info             |
//+----+------+-----------------+---------+---------+------+-------+------------------+
//| 18 | root | localhost       | NULL    | Query   |    0 | init  | show processlist |
//| 23 | root | localhost       | NULL    | Sleep   |  875 |       | NULL             |
//| 24 | root | localhost:37455 | student | Sleep   |   32 |       | NULL             |
//+----+------+-----------------+---------+---------+------+-------+------------------+
//3 rows in set (0.00 sec)
//最后发现status命令揭晓了答案:mysql> status;
//Connection:		Localhost via UNIX socket
//UNIX socket:		/var/lib/mysql/mysql.sock

//频繁的连接断开数据库会导致系统中存在大量的TIME_WAIT,浪费系统资源
//采用连接池的方式创建连接,能有效回收数据库使用完成的连接资源,而且阻止了频繁的创建连接,
//降低系统资源消耗
//[root@localhost student]# netstat -na | grep 3306 | wc -l
//401
//[root@localhost student]# netstat -na | grep 3306
//tcp        0      0 127.0.0.1:37454         127.0.0.1:3306          ESTABLISHED
//tcp        0      0 127.0.0.1:37453         127.0.0.1:3306          TIME_WAIT
//tcp6       0      0 :::3306                 :::*                    LISTEN
//tcp6       0      0 127.0.0.1:3306          127.0.0.1:37454         ESTABLISHED

//使用连接池的n个问题：
//1）连接池只能针对某个用户和某个数据库进行连接,连接池只能容纳一种用户的连接
//2）连接池能中的连接如果失效怎么办?
//3）如果连接失效,那么通过mysql_ping尝试再次进行连接
//4*) 如果连接还没回收到内存中,进程就异常退出,中间丢失的连接内存和连接池中的内存怎么管理?
// 	  要不连接池还是通过vector管理(最后统一释放),否则中途pop(进程奔溃会丢失这个连接的内存没有释放),
//5*) 如果连接池的连接不够用,还需要在mysqlIntf维护一个非连接池的连接吧,虽然可能频繁导致
//	 TIME_WAIT,总比没有连接可用好

//GetInstance为静态函数,不需要初始化对象也能直接调用
//CConnectPool* s_pInstance = CConnectPool::GetInstance();

//构造函数
CMysqlHandle::CMysqlHandle()
{
	_isConnected = false;
	_pRes = NULL;
	//初始化SQL对象
	_pConnect = NULL;
}

//析构函数
CMysqlHandle::~CMysqlHandle()
{
	_isConnected = false;
	if(_pRes != NULL)
	{
		mysql_free_result(_pRes);
		_pRes = NULL;
	}
	//关闭数据库,就算数据库没有连接也不会出现问题
	//mysql_close(_pConnect);
	//_pConnect内存由连接池进行管理
}


//连接数据库
int CMysqlHandle::Connect(const char* pHost,
						  const short port,
						  const char* pUser,
						  const char* pPassword,
						  const char* pDbname)
{
	MYSQL* pSQL = NULL;
	//连接已经存在就直接返回
	if(_isConnected)
	{
		return 0;
	}
	//连接数据库,不需要初始化数据库mysql_init,因为_connect已经分配了数据库对象
	//从连接池获取连接,不能直接用_pConnect复制,因为_pConnect置为空则
	pSQL = CConnectPool::GetInstance()->GetConnection(pHost, port, pUser, pPassword, pDbname);
	//pSQL = CConnectPool::GetConnection(pHost, port, pUser, pPassword, pDbname);
	if(NULL == pSQL)
	{
		printf("mysql_real_connect errr\n");
		return -1;
	}
	//设置连接成功标志
	_isConnected = true;
	_pConnect = pSQL;
	return 0;
}

//尝试重新连接,连接成功返回0,SQL语句执行失败有可能是SQL语句本身问题,也有可能是连接的问题
int CMysqlHandle::ReConnect(void)
{
	if(mysql_ping(_pConnect) != 0)
	{
		return -1;
	}
	return 0;
}

//尝试重连方式的SQL语句执行
int CMysqlHandle::QuerySQL(const char* pSQL, int len)
{
	//执行SQL语句
	if(mysql_real_query(_pConnect, pSQL, len) != 0)
	{
		//尝试重新连接,但是重连失败
		if(ReConnect() != 0)
		{
			printf("mysql_real_query ReConnect err\n");
			return -1;
		}
		//重新连接成功,再次执行一次SQL语句
		if(mysql_real_query(_pConnect, pSQL, len) != 0)
		{
			//SQL语句本身错误或者结果集太大导致内存不够?
			printf("mysql_real_query SQL err\n");
			return -1;
		}
	}

	return 0;
}

//查询数据库,输出结果集
int CMysqlHandle::SelectQuery(const char* pSQL, T_SQL_RECORD& result)
{
	int re = 0;

	//清空输出信息
	result.nField = 0;
	result.pField = NULL;
	result.records.clear();
	//没有连接则直接返回
	if(!_isConnected)
	{
		printf("SelectQuery no connection err\n");
		return -1;
	}
	//执行SQL查询语句
	re = QuerySQL(pSQL, strlen(pSQL));
	//不管成功还是失败,先回收连接到连接池
	CConnectPool::GetInstance()->RecoverConnection(_pConnect);
	if(re != 0)
	{
		printf("SelectQuery err\n");
		return -1;
	}
	//首先清空上次查询的结果
	if(_pRes != NULL)
	{
		mysql_free_result(_pRes);
		_pRes = NULL;
	}
	//存放结果集
	if(NULL == (_pRes = mysql_store_result(_pConnect)))
	{
		printf("mysql_store_result err\n");
		return -1;
	}
	//存储字段属性信息
	result.nField = mysql_num_fields(_pRes);
	result.pField = mysql_fetch_fields(_pRes);
	//存储结果集信息
	MYSQL_ROW row;
	while((row = mysql_fetch_row(_pRes)) != NULL)
	{
		result.records.push_back(row);
	}

	return 0;
}

//修改数据库
int CMysqlHandle::ModifyQuery(const char* pSQL)
{
	int re = 0;
	//没有建立连接,直接返回
	if(!_isConnected)
	{
		return -1;
	}
	//执行SQL查询语句
	re = QuerySQL(pSQL, strlen(pSQL));
	//不管成功还是失败,先回收连接到连接池
	CConnectPool::GetInstance()->RecoverConnection(_pConnect);
	if(re != 0)
	{
		printf("SelectQuery (Modify) err\n");
		return -1;
	}
	return 0;
}

//显示结果
void CMysqlHandle::ShowResult(T_SQL_RECORD& result)
{
	//打印字段名称
	for(int i = 0; i < result.nField; i++)
	{
		printf("%s\t", result.pField[i].name);
	}
	printf("\n");
	//打印结果集
	vector<MYSQL_ROW>::const_iterator it;
	for(it = result.records.begin(); it != result.records.end(); ++it)
	{
		for(int i = 0; i < result.nField; i++)
		{
			printf("%s\t", (*it)[i]);
		}
	}
	printf("\n");
}

//测试多个连接,用netstat检查3306端口是否存在过多的TIME_WAIT状态
void TestMutliConnect(void)
{
	CMysqlHandle sql;
	T_SQL_RECORD result;

	//连接数据库
	sql.Connect("127.0.0.1", MYSQL_PORT, "root", NULL, "student");
	//SQL查询
	sql.SelectQuery(QUERY_LIDI_RECORD, result);
	//打印查询结果
	sql.ShowResult(result);
}

//测试代码参考
int TestSQL(void)
{
	//连接数据库
	MYSQL* pMysql;
	MYSQL_RES* pResult;
	MYSQL_ROW row;
	MYSQL_FIELD* pField;
	int nCol = 0;
	int nRow = 0;

	//初始化数据库
	if( NULL == ( pMysql = mysql_init(NULL) ) )
	{
		printf( "sql init err\n" );
		return -1;
	}

	//连接数据库
	if( NULL == mysql_real_connect( pMysql, "localhost", "root", NULL, "testOath", 3306, NULL, 0 ) )
	{
		printf( "sql connect err\n" );
		return -1;
	}

	printf("%s-%s-%s\n", pMysql->host, pMysql->user, pMysql->db);

	//查询所有数据
	if( mysql_query( pMysql, "SELECT * FROM pet" ) != 0 )
	{
		printf( "sql query no return\n" );
	}
	else
	{
		//存储查询结果
		if( NULL == ( pResult = mysql_store_result( pMysql ) ) )
		{
			printf( "sql query err\n" );
			return -1;
		}

		//显示查询结果
		nRow = mysql_num_rows( pResult );
		nCol = mysql_num_fields( pResult );
		printf( "sql query %d lines\n", nRow );
		while( ( row = mysql_fetch_row( pResult ) ) != NULL )
		{
			//逐行显示
			for( int i = 0; i < nCol; i++ )
			{
				if( ( pField = mysql_fetch_field_direct( pResult, i ) ) != NULL )
				{
					printf( "%s: %-8s, ", pField->name, row[i] );
				}
			}
			printf( "\n" );
		}

		//释放数据库相关的内存
		mysql_free_result( pResult );
	}

	mysql_close( pMysql );

	return 0;
}

int main( void )
{
	printf("hello world\n");

	//初始化连接池内存
	CConnectPool* pPool = CConnectPool::GetInstance();
	//TestSQL();
	for(int i = 0; i < 1000; i++)
	{
		TestMutliConnect();
	}
	//需要手动方式回收内存?
	pPool->DestoryConnectPool();

	return 0;
}




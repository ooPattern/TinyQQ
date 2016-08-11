/*
 * main.cc
 *
 *  Created on: Aug 5, 2016
 *      Author: root
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "SQL/mysqlIntf.h"
#include "SQL/connectPool.h"
#include "ThreadPool/threadPool.h"

#define QUERY_LIDI_RECORD ("SELECT * FROM pupil WHERE name='lidi';")
#define QUERY_YANGYONG_RECORD ("SELECT * FROM pupil WHERE name='yangyong';")

//任务1: 测试多个连接,用netstat检查3306端口是否存在过多的TIME_WAIT状态
void Task1_MutliConnect(void*)
{
	CMysqlHandle sql;
	T_SQL_RECORD result;

	//连接数据库
	if(0 == sql.Connect("127.0.0.1", MYSQL_PORT, "root", NULL, "student"))
	{
		//模拟进程崩溃,检查是否会调用析构函数
		//abort();

		//SQL查询语句1
		sql.SelectQuery(QUERY_LIDI_RECORD, result);
		sql.ShowResult(result);
		//SQL查询语句2
		sql.SelectQuery(QUERY_YANGYONG_RECORD, result);
		sql.ShowResult(result);
	}

}



//任务2: 打印线程ID, 测试线程池是否执行了该任务
void Task2_PrintTID(void*)
{
	static int s_sum = 0;
	printf("cycle=%d : Task2_PrintTID tid=0x%lx\n", ++s_sum, pthread_self());
	sleep(1);
}

int main( void )
{
	int re = 0;
	T_TASK task;

	//TestSQL(NULL);
	printf("hello world\n");
	//初始化连接池内存,避免频繁连接和断开数据库
	CConnectPool* pConnPool = CConnectPool::GetInstance();

	//初始化线程池,线程池必须初始化后才能使用
	CThreadPool threadPool;
	re = threadPool.InitThreadPool(10);
	if(0 == re)
	{
		//添加20个线程打印ID任务
		for(int i = 0; i < 20; i++)
		{
			task.pCtx = NULL;
			task.pHandle = Task2_PrintTID;
			threadPool.AddTaskHandle(task);
		}
		//添加1000个数据库查询任务
		for(int i = 0; i < 1000; i++)
		{
			task.pCtx = NULL;
			task.pHandle = Task1_MutliConnect;
			threadPool.AddTaskHandle(task);
		}
	}

	//如果数据库查询任务没有执行完毕就释放连接池,会导致数据库查询任务异常
	//等待线程池所有任务执行完毕,如果线程池包含数据库查询任务,不能释放连接池,这样数据库查询数据会异常
	while(!threadPool.IsAllTaskFinish())
	{
		//任务队列中如果包含数据库查询,如果不等待任务执行完毕就释放连接池内存
		//会导致正在查询的数据库任务发生错误
		printf("main all task do not finish?\n");
		sleep(2);
	}

	//需要手动方式回收连接池内存?,是的,不然会内存泄露
	pConnPool->DestoryConnectPool();
	printf("go to end?\n");

	return 0;
}


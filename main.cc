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

//任务1: 测试多个连接,用netstat检查3306端口是否存在过多的TIME_WAIT状态
void Task1_MutliConnect(void*)
{
	CMysqlHandle sql;
	T_SQL_RECORD result;

	//连接数据库
	sql.Connect("127.0.0.1", MYSQL_PORT, "root", NULL, "student");

	//模拟进程崩溃,检查是否会调用析构函数
	//abort();

	//SQL查询
	sql.SelectQuery(QUERY_LIDI_RECORD, result);
	//打印查询结果
	sql.ShowResult(result);
}

//任务2: 打印线程ID, 测试线程池是否执行了该任务
void Task2_PrintTID(void*)
{
	printf("Task2_PrintTID tid=0x%lx\n", pthread_self());
}

int main( void )
{
	int re = 0;
	T_TASK task;

	printf("hello world\n");
	//初始化连接池内存,避免频繁连接和断开数据库
	//CConnectPool* pConnPool = CConnectPool::GetInstance();

	//初始化线程池,线程池必须初始化后才能使用
	CThreadPool threadPool;
	re = threadPool.InitThreadPool(10);
	if(0 == re)
	{
		//添加数据库查询任务和其他任务
		for(int i = 0; i < 1; i++)
		{
			//添加数据库查询任务
			//task.pCtx = NULL;
			//task.pHandle = Task1_MutliConnect;
			//threadPool.AddTaskHandle(task);
			//添加线程打印ID任务
			task.pCtx = NULL;
			task.pHandle = Task2_PrintTID;
			threadPool.AddTaskHandle(task);
		}
	}

	//发现存在2个问题:
	//1) 不加sleep,进程直接退出,不执行刚添加的任务
	//2) 加sleep,进程下的线程会执行任务队列的任务,但是sleep返回后线程没有正常退出(没有调用析构?)
	sleep(3);

	printf("go to end?\n");

	//需要手动方式回收连接池内存?
	//pConnPool->DestoryConnectPool();

	return 0;
}


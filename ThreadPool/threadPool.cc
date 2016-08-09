/*
 * threadPool.cc
 *
 *  Created on: Aug 4, 2016
 *      Author: root
 */

#include <stdio.h>
#include <assert.h>
#include "threadPool.h"

//构造函数
CThreadPool::CThreadPool()
{
    
}
    
//析构函数
CThreadPool::~CThreadPool()
{
    DestoryThreadPool();
}

//初始化内存池
int CThreadPool::InitThreadPool(int threadNum)
{
    int err = 0;

    //初始化线程池销毁标志
    _shutdown = false;
    //清空任务队列
    _taskQueue.clear();
    //初始化互斥锁
    err = pthread_mutex_init(&_lock, NULL);
    if(err != 0)
    {
        printf("pthread_mutex_init faid err=%d\n", err);
        return -1;
    }
    //初始化条件变量
    err = pthread_cond_init(&_cond, NULL);
    if(err != 0)
    {
        printf("pthread_cond_init faid err=%d\n", err);
        return -1;
    }
    //限制最大线程数量
    if(threadNum > THREAD_MAX_NUM)
    {
        threadNum = THREAD_MAX_NUM;
    }
    _threadNum = threadNum;
    //创建线程
    pthread_t tid;
    for(int i = 0; i < _threadNum; i++)
    {
        //创建线程时指定了线程启动例程
    	//StartRoutine必须是静态函数,因为静态函数要求在内存只有一份实例
        err = pthread_create(&tid, NULL, StartRoutine, (void*)this);
        if(err != 0)
        {
            printf("pthread_create faid err=%d\n", err);
            return -1;
        }
        //创建线程成功则添加线程tid,tid用于以后销毁线程的参数
        _tid.push_back(tid);
    }

    return 0;
}

//销毁线程池
void CThreadPool::DestoryThreadPool(void)
{
    //防止2次调用,不能重复销毁线程池
    if(_shutdown)
    {
        return;
    }
    _shutdown = true;

    //唤醒所有等待线程,线程要销毁了
    pthread_cond_broadcast(&_cond);

    //阻塞等待线程池所有线程退出,否则有可能会变成僵尸进程
    vector<pthread_t>::const_iterator it;
    for(it = _tid.begin(); it != _tid.end(); ++it)
    {
        pthread_join(*it, NULL);
    }
    _taskQueue.clear();
    printf("DestoryThreadPool all thread quit finish\n");

    //销毁任务队列
    _taskQueue.clear();

    //销毁互斥锁和条件变量
    pthread_mutex_destroy(&_lock);
    pthread_cond_destroy(&_cond);

}

//静态函数表示内存只有一份实例,pthread_create()要求参数必须是静态函数
void* CThreadPool::StartRoutine(void* pArg)
{
	CThreadPool* pInstance = (CThreadPool*)pArg;
	pInstance->ThreadRoutine();
	return (void*)0;
}

//线程互斥量加锁
int CThreadPool::MutexLock(pthread_mutex_t* pMutex)
{
    int err = 0;
    err = pthread_mutex_lock(pMutex);
    if(err != 0)
    {
        printf("pthread_mutex_lock faid err=%d\n", err);
        return -1;
    }
    return 0;
}
//线程互斥量解锁
int CThreadPool::MutexUnlock(pthread_mutex_t* pMutex)
{
    int err = 0;
    err = pthread_mutex_unlock(pMutex);
    if(err != 0)
    {
        printf("pthread_mutex_unlock faid err=%d\n", err);
        return -1;
    }
    return 0;
}
//线程条件变量等待
int CThreadPool::CondWait(pthread_cond_t* pCond, pthread_mutex_t* pMutex)
{
    int err = 0;
    err = pthread_cond_wait(pCond, pMutex);
    if(err != 0)
    {
        printf("pthread_cond_wait faid err=%d\n", err);
        return -1;
    }
    return 0;
}
//线程条件变量唤醒
int CThreadPool::CondSignal(pthread_cond_t* pCond)
{
    int err = 0;
    err = pthread_cond_signal(pCond);
    if(err != 0)
    {
        printf("pthread_cond_signal faid err=%d\n", err);
        return -1;
    }
    return 0;
}

//线程启动例程,判断线程是否空闲,空闲就处理任务队列的任务
void CThreadPool::ThreadRoutine(void)
{
    //暂时没有处理信号
    T_TASK task;

    for(;;)
    {
        //尝试加锁,配合条件变量
        MutexLock(&_lock);
        
        //检查任务队列是否为空,如果为空则继续等待任务
        while(_taskQueue.empty())
        {
            CondWait(&_cond, &_lock);
        }
        
        //如果应用层销毁线程池,需要安全的退出线程(_shutdown标志通知),
        //否则在销毁的地方pthread_join则无法获取线程退出的状态,
        if(_shutdown)
        {
        	//printf("pthread tid=0x%lx go to exit...\n", pthread_self());
        	//别忘记退出线程前(返回前)释放锁,加锁和释放锁必须配对使用
        	MutexUnlock(&_lock);
        	//或者调用return,但是不能调用exit(),因为那会把整个进程都退出
        	pthread_exit(NULL);
        }

        //空闲线程开始处理任务
        //printf("pthread tid=0x%lx start to work\n", (unsigned long)pthread_self());
        assert(!_taskQueue.empty());
        
        //取出队列元素
        task = _taskQueue.front();
        _taskQueue.pop_front();
        
        //尝试解锁
        MutexUnlock(&_lock);
        
        //调用回调函数,执行任务
        (*(task.pHandle))(task.pCtx);
    }

}

//添加任务到任务队列,线程池中的线程在空闲的时候会执行该任务
void CThreadPool::AddTaskHandle(const T_TASK& task)
{
    //操作任务(添加或者删除)需要添加锁,
    //因为多个线程同时在操作任务队列,必须进行同步保护
    MutexLock(&_lock);

    if(_taskQueue.size() <  TASK_MAX_NUM)
    {
        //将任务添加到队列中,在线程空闲的时候执行
        _taskQueue.push_back(task);
    }
    else
    {
        //打印错误警告信息
        printf("AddTaskHandle task queue is full err\n");
    }

    //操作完队列后需要释放锁
    MutexUnlock(&_lock);

    //唤醒一个等待线程,如果所有线程都在忙碌,这句话没有作用
    CondSignal(&_cond);
}





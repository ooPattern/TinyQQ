/*
 * threadPool.cc
 *
 *  Created on: Aug 4, 2016
 *      Author: root
 */

#include <stdio.h>
#include <assert.h>
#include "threadPool.h"

/*
 * 实验1:
 * 负载严重不平衡啊,5000个任务,1个线程执行了4991次,其他的线程才执行了一次,
 * 是不是由于任务太简单了(只有一句打印语句)?导致一直都是一个线程在运行?
 * 可以尝试增加几个耗时的任务后再统计一下线程的运行情况?
 * -----------------THREAD LOAD--------------------
 * TID:				RUN_TIMES:
 * 0x7FAECED0C700			1
 * 0x7FAECE50B700			1
 * 0x7FAECDD0A700			1
 * 0x7FAECD509700			4991
 * 0x7FAECCD08700			1
 * 0x7FAECC507700			1
 * 0x7FAECBD06700			1
 * 0x7FAECB505700			1
 * 0x7FAECAD04700			1
 * 0x7FAECA503700			1
 * -----------------THREAD END----------------------
 *
 * 实验2:
 * 如果打印语句后面增加一个sleep(1),运行20个任务,每个线程都能分配到任务执行
 * 说明线程池处理任务得到负载平衡,没有导致任务全压在一个线程执行,线程池起到作用了
 * -----------------THREAD LOAD--------------------
 * TID:				RUN_TIMES:
 * 0x7F726173A700			2
 * 0x7F7260F39700			2
 * 0x7F7260738700			2
 * 0x7F725FF37700			2
 * 0x7F725F736700			2
 * 0x7F725EF35700			2
 * 0x7F725E734700			2
 * 0x7F725DF33700			2
 * 0x7F725D732700			2
 * 0x7F725CF31700			2
 * -----------------THREAD END----------------------
 *
 * 实验3:(BUG已修复)
 * 加载完任务后进程直接退出(调用线程池析构,main返回),发现任务没有执行
 * 分析:
 *   在线程启动历程ThreadRoutine()没有等待任务执行完毕就直接退出了,增加了等待任务处理标志后问题,
 *   就算任务队列为空,也必须等待所有任务执行完毕后才允许线程退出,否则所有线程必须继续运行
 *
 * 实验4:(BUG已修复)
 * 加载完任务后sleep,执行完成任务后,调用线程池析构函数发现线程没有退出,
 * 阻塞在pthread_join,得不到线程退出
 * 分析:
 *    执行任务完毕后,在线程启动历程ThreadRoutine()判断队列为空,继续挂起线程等待,遗漏了用户要退出线程这种
 *    情况,导致一直没有退出线程,所以销毁线程池时线程析构函数会一直阻塞在pthread_join.
 *
 * */

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
    //初始化等待任务数量,0表示所有任务执行完毕
    //如果线程池销毁,需要等待所有任务执行完毕后才能销毁线程池
    _waitting = 0;
    //清空线程ID信息
    _tid.clear();
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
    T_THREAD_ID tid;
    for(int i = 0; i < _threadNum; i++)
    {
        //创建线程时指定了线程启动例程
    	//StartRoutine必须是静态函数,因为静态函数要求在内存只有一份实例
        err = pthread_create(&(tid.id), NULL, StartRoutine, (void*)this);
        if(err != 0)
        {
            printf("pthread_create faid err=%d\n", err);
            return -1;
        }
        //创建线程成功则添加线程tid,tid用于以后销毁线程的参数
        tid.runTimes = 0;
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
    vector<T_THREAD_ID>::const_iterator it;
    for(it = _tid.begin(); it != _tid.end(); ++it)
    {
        pthread_join((*it).id, NULL);
    }

    //线程池销毁前统计每个线程运行的次数,作为线程负载考量
    printf("DestoryThreadPool all thread quit finish\n");
    CalcThreadLoad();

    //如果全部线程没有退出完成,不会运行到这里
    _tid.clear();

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
    //暂时没有处理信号???
    T_TASK task;
    pthread_t tid;

    for(;;)
    {
        //尝试加锁,配合条件变量
        MutexLock(&_lock);
        
        //检查任务队列是否为空,如果为空则继续等待任务
        //一定要加上_shutdown条件的判断,因为如果任务队列为空时
        //如果销毁线程池,会一直阻塞在这个地方,导致线程无法正常退出
        while((!_shutdown) && (_taskQueue.empty()))
        {
            CondWait(&_cond, &_lock);
        }
        
        //如果应用层销毁线程池,需要安全的退出线程(_shutdown标志通知),
        //否则在销毁的地方pthread_join则无法获取线程退出的状态,
        //如果队列不为空时要销毁线程,必须等待队列的任务完成后再销毁线程
        if((_shutdown) && (_taskQueue.empty()))
        {
        	//printf("pthread tid=0x%lx go to exit...\n", pthread_self());
        	//判断是否有线程还没有完成任务的执行,如果是,等待它完成任务后再退出线程吧
			if(_waitting > 0)
			{
				//先别退出线程,这种情况还是等待其他线程执行任务完毕再一起退出线程吧
				//记得返回前加锁和解锁需要配对使用,所以这里记得解锁
				MutexUnlock(&_lock);
				continue;
			}
			//所有任务已经执行完毕,可以安全退出线程
			else
			{
				//别忘记退出线程前(返回前)释放锁,加锁和释放锁必须配对使用
				MutexUnlock(&_lock);
				//或者调用return,但是不能调用exit(),因为那会把整个进程都退出
				pthread_exit(NULL);
			}
        }

        //空闲线程开始处理任务
        //printf("pthread tid=0x%lx start to work\n", (unsigned long)pthread_self());
        assert(!_taskQueue.empty());
        
        //取出队列元素
        task = _taskQueue.front();
        _taskQueue.pop_front();
        
        //在对应线程增加运行次数
        vector<T_THREAD_ID>::iterator it;
        tid = pthread_self();
        for(it = _tid.begin(); it != _tid.end(); ++it)
        {
        	if(pthread_equal((*it).id, tid) != 0)
        	{
        		(*it).runTimes++;
        	}
        }

        //尝试解锁
        MutexUnlock(&_lock);
        
        //调用回调函数,执行任务
        (*(task.pHandle))(task.pCtx);

        //执行完任务后,需要更新未完成任务数量
        //必须等待所有任务完成后才能销毁线程池
        //这样操作锁粒度是否太小了,导致频繁的加锁和解锁?
        MutexLock(&_lock);
        if(_waitting > 0)
        {
        	_waitting--;
        }
        MutexUnlock(&_lock);
    }

}

//添加任务到任务队列,线程池中的线程在空闲的时候会执行该任务
void CThreadPool::AddTaskHandle(const T_TASK& task)
{
	//销毁线程池后不能添加任务,
	//必须用InitThreadPool重新初始化线程池后才能继续添加任务
	if(_shutdown)
	{
		printf("ThreadPool already destroy, please init again\n");
		return;
	}

    //操作任务(添加或者删除)需要添加锁,
    //因为多个线程同时在操作任务队列,必须进行同步保护
    MutexLock(&_lock);
    //限制最大任务个数
    if(_taskQueue.size() <  TASK_MAX_NUM)
    {
    	//更新未完成的任务数量,销毁线程池时必须等待所有任务执行完毕才能销毁
    	//类似任务队列,操作该数据同样用互斥锁进行同步
		if(_waitting <= 0)
		{
			_waitting = 0;
		}
		_waitting++;
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

//检查是否当前所有任务已经处理完成
bool CThreadPool::IsAllTaskFinish(void)
{
	return ((_waitting > 0) ? (false) : (true));
}

//查看线程的运行情况,统计线程的负载是否均衡,计算每个线程运行的任务数量
void CThreadPool::CalcThreadLoad(void)
{
	int total = 0;

	printf("-----------------THREAD LOAD--------------------\n");
	printf("TID:				RUN_TIMES:\n");

	vector<T_THREAD_ID>::const_iterator it;
	for(it = _tid.begin(); it != _tid.end(); ++it)
	{
		printf("0x%lX			%d\n", (*it).id, (*it).runTimes);
		total += (*it).runTimes;
	}
	printf("\nTOTAL TASK:			%d\n", total);
	printf("-----------------THREAD END----------------------\n");
}





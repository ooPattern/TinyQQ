/*
 * threadPool.h
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */

#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <vector>
#include <deque>
#include <pthread.h>


using std::vector;
using std::deque;

//线程任务
typedef struct {
    void* pCtx;//任务处理函数的入口参数
    void (*pHandle)(void* pArg);//任务处理函数
} T_TASK;

//统计线程运行次数最好不要用map,虽然线程ID唯一,但是感觉不好扩展更多的信息
typedef struct {
	pthread_t id;//线程ID
	int runTimes;//线程运行次数
} T_THREAD_ID;

//线程池类,任务是否需要添加优先级,让高优先级的任务先执行
class CThreadPool {
    //线程池最大的线程数量
    const int THREAD_MAX_NUM = 20;
    //最大任务数量,表示最多支持执行任务的数量,超过该数量就不能再添加任务了
    const unsigned int TASK_MAX_NUM = 50000;

public:
    //构造函数
    CThreadPool();
    //析构函数
    ~CThreadPool();

    //添加任务到任务队列,线程池中的线程在空闲的时候会执行该任务
    void AddTaskHandle(const T_TASK& task);
    //初始化线程池,只有初始化线程池后才能添加任务,
    //如果初始化线程池失败,则不能使用线程池
    int InitThreadPool(int threadNum);
    //销毁线程池,注意销毁线程池后就不能再使用线程池,因为已经释放了内存
    void DestoryThreadPool(void);
    //查看线程的运行情况,统计线程的负载是否均衡,计算每个线程运行的任务数量
    void CalcThreadLoad(void);
    //检查是否当前所有任务已经处理完成
    bool IsAllTaskFinish(void);

private:
    bool _shutdown;//判断线程池是否已经销毁,不能重复销毁
    //队列等待任务,线程池销毁前必须等待队列中所有任务执行完成后才能销毁
    //该值表示等待的任务,为0表示所有任务执行完成
    int _waitting;

    pthread_mutex_t _lock;//互斥锁用于同步任务队列的添加和删除
    pthread_cond_t _cond;//条件变量,和互斥锁配合使用,满足条件才唤醒线程处理任务
    
    int _threadNum;//线程数量
    vector<T_THREAD_ID> _tid;//线程id,退出线程时通过线程id进行广播唤醒线程

    deque<T_TASK> _taskQueue;//任务队列,需要执行的任务会添加的任务队列中,在线程空闲的时候执行

    //线程启动例程,每个线程都做一样的事情,如果空闲的话就处理任务队列的任务
    static void* StartRoutine(void* pArg);
    void ThreadRoutine(void);

    //线程互斥量加锁
    int MutexLock(pthread_mutex_t* pMutex);
    //线程互斥量解锁
    int MutexUnlock(pthread_mutex_t* pMutex);
    //线程条件变量等待
    int CondWait(pthread_cond_t* pCond, pthread_mutex_t* pMutex);
    //线程条件变量唤醒
    int CondSignal(pthread_cond_t* pCond);

    //no copy
	CThreadPool(const CThreadPool&);
	CThreadPool& operator=(const CThreadPool&);
};

#endif /* THREADPOOL_H_ */

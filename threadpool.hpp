#pragma once
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
#include "http_conn.h"

template<class T>
class threadpool{
public:
    // thread_number-线程数量  max_requests-最大请求数量
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    // 添加任务到请求队列中
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();

private:
    // 是否结束线程, true-结束线程
    bool m_stop;
    // 描述线程池的数组
    pthread_t* m_threads;
    // 线程的数量
    int m_thread_number;
    // 请求队列中最多允许的、等待处理的请求数量
    int m_max_requests;
    // 保护请求队列的互斥锁
    locker m_queuelocker;
    // 请求队列
    list<T*> m_workqueue;
    // 待处理的任务通知信号，表示是否有任务需要处理
    sem m_queuestat;
};

template<class T>
// 初始化变量
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw exception();
    }
    // 创建存储线程数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw exception();
    }
    // 创建线程, 将线程设置为脱离状态
    for(int i = 0; i < m_thread_number; i++)
    {
        printf("create the %dth thread\n", i);
        // 若创建失败
        // this表示传入该线程池指针
        if(pthread_create(m_threads+i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw exception();
        }
        // 创建成功，则标记分离一个线程。被标记的线程在终止的时候，会自动释放资源返回给系统
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw exception();
        }
    }
}

// 析构函数
template<class T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

// 添加任务到请求队列中
template<class T>
bool threadpool<T>::append(T* request)
{
    // 因为请求队列(m_workqueue)，所以需要加锁，表示接下来的代码只允许该线程使用
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    // 插入一个待办的任务
    m_workqueue.push_back(request);
    // 解锁
    m_queuelocker.unlock();
    // 新增一个信号灯，表示告诉内核有一个任务需要处理
    m_queuestat.post();
    return true;
}

template<class T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

// 工作线程的运行函数，用于处理代办任务
template<class T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        // 需要有待处理的任务(也可以称为空闲信号灯)才能继续，否则将一直阻塞在此处
        // 只有上面第97行代码 m_queuestat.post() 执行了，代码才能继续往下走
        m_queuestat.wait();
        // 禁止其它线程使用请求队列
        m_queuelocker.lock();
        // 如果请求队列是空的
        if(m_workqueue.size() <= 0)
        {
            m_queuelocker.unlock();
            continue;
        }

        // 如果请求队列不是空的
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        // 请求队列已经弹出一个任务, 可以解锁该队列
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        // 该工作线程正式工作
        request->process();
    }
}

#pragma once

#include <exception>
#include <pthread.h>
#include <semaphore.h>
using namespace std;

// 互斥锁
class locker{
public:
    locker()
    {
        // 如果不能成功创建互斥量就报错
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    // 加锁
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // 解锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t* get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 条件变量
class cond{
public:
    cond()
    {
        // 如果不能成功创建互斥量就报错
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw exception();
        }
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    // 等待
    bool wait(pthread_mutex_t* m_mutex)
    {
        int ret = pthread_cond_wait(&m_cond, m_mutex);
    }

    // 解锁
    bool timewait(pthread_cond_t m_cond, pthread_mutex_t* m_mutex, struct timespec time)
    {
        int ret = pthread_cond_timedwait(&m_cond, m_mutex, &time);
        return ret == 0;
    }
    // 唤醒一个或者多个等待的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    // 唤醒所有的等待的线程
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

// 信号灯
class sem{
public:
    // 初始化信号量
    sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0)
        {
            throw exception();
        }
    }

    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0)
        {
            throw exception();
        }
    }

    ~sem()
    {
        sem_destroy(&m_sem);
    }

    // 消耗信号量
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // 增加信号量
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

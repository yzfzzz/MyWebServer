#pragma once

#include <exception>
#include <pthread.h>
#include <semaphore.h>
using namespace std;

// ������
class locker{
public:
    locker()
    {
        // ������ܳɹ������������ͱ���
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    // ����
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // ����
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

// ��������
class cond{
public:
    cond()
    {
        // ������ܳɹ������������ͱ���
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw exception();
        }
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }

    // �ȴ�
    bool wait(pthread_mutex_t* m_mutex)
    {
        int ret = pthread_cond_wait(&m_cond, m_mutex);
    }

    // ����
    bool timewait(pthread_cond_t m_cond, pthread_mutex_t* m_mutex, struct timespec time)
    {
        int ret = pthread_cond_timedwait(&m_cond, m_mutex, &time);
        return ret == 0;
    }
    // ����һ�����߶���ȴ����߳�
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    // �������еĵȴ����߳�
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

// �źŵ�
class sem{
public:
    // ��ʼ���ź���
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

    // �����ź���
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    // �����ź���
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

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
    // thread_number-�߳�����  max_requests-�����������
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    // ����������������
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();

private:
    // �Ƿ�����߳�, true-�����߳�
    bool m_stop;
    // �����̳߳ص�����
    pthread_t* m_threads;
    // �̵߳�����
    int m_thread_number;
    // ����������������ġ��ȴ��������������
    int m_max_requests;
    // ����������еĻ�����
    locker m_queuelocker;
    // �������
    list<T*> m_workqueue;
    // �����������֪ͨ�źţ���ʾ�Ƿ���������Ҫ����
    sem m_queuestat;
};

template<class T>
// ��ʼ������
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw exception();
    }
    // �����洢�߳�����
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw exception();
    }
    // �����߳�, ���߳�����Ϊ����״̬
    for(int i = 0; i < m_thread_number; i++)
    {
        printf("create the %dth thread\n", i);
        // ������ʧ��
        // this��ʾ������̳߳�ָ��
        if(pthread_create(m_threads+i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw exception();
        }
        // �����ɹ������Ƿ���һ���̡߳�����ǵ��߳�����ֹ��ʱ�򣬻��Զ��ͷ���Դ���ظ�ϵͳ
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw exception();
        }
    }
}

// ��������
template<class T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

// ����������������
template<class T>
bool threadpool<T>::append(T* request)
{
    // ��Ϊ�������(m_workqueue)��������Ҫ��������ʾ�������Ĵ���ֻ������߳�ʹ��
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    // ����һ�����������
    m_workqueue.push_back(request);
    // ����
    m_queuelocker.unlock();
    // ����һ���źŵƣ���ʾ�����ں���һ��������Ҫ����
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

// �����̵߳����к��������ڴ����������
template<class T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        // ��Ҫ�д����������(Ҳ���Գ�Ϊ�����źŵ�)���ܼ���������һֱ�����ڴ˴�
        // ֻ�������97�д��� m_queuestat.post() ִ���ˣ�������ܼ���������
        m_queuestat.wait();
        // ��ֹ�����߳�ʹ���������
        m_queuelocker.lock();
        // �����������ǿյ�
        if(m_workqueue.size() <= 0)
        {
            m_queuelocker.unlock();
            continue;
        }

        // ���������в��ǿյ�
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        // ��������Ѿ�����һ������, ���Խ����ö���
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        // �ù����߳���ʽ����
        request->process();
    }
}

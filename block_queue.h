#pragma once
#include <iostream>
#include <pthread.h>
#include "locker.h"

template <class T>
class block_queue{
public:
    block_queue(int max_size = 1000)
    {
        if(max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue()
    {
        m_mutex.lock();
        if(m_array != NULL)
        {
            delete[] m_array;
        }
        m_mutex.unlock();
    }

    // 判断队列是否满了
    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 判断队列是否为空
    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value)
    {
        m_mutex.lock();
        if(m_size > 0)
        {
            value = m_array[m_front];
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队尾元素
    bool back(T &value)
    {
        m_mutex.lock();
        if(m_size > 0)
        {
            value = m_array[m_back];
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队列的长度
    int size()
    {
        int tmp;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    //当有元素push进队列,相当于生产者生产了一个元素, 则需要将所有使用队列的线程（消费者）先唤醒，处理这些任务
    bool push(const T &item)
    {
        m_mutex.lock();
        // 循环队列中的任务都没有处理完
        if(m_size >= m_max_size)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back+1)%m_max_size;
        m_array[m_back] = item;

        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();

        while(m_size <= 0)
        {
            if(!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front+1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};
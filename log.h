#pragma once
#include "block_queue.h"
#include <string.h>
#include <sys/time.h>
#include "locker.h"
using namespace std;

class Log
{
public:
    // ����log�����ָ��
    static Log *get_instance()
    {
        /* 
            instanceֻ�ܳ�ʼ��һ�Σ����Լ������ get_instance() ������Σ�
            ����static Log instance;ִֻ��һ�Σ��� instance �ĵ�ַ���ᱻ�ı� 
        */
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void* arg)
    {
        Log::get_instance()->async_write_log();
    }

    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    void write_log(int level, const char* format, ...);
    void flush();

private:
    long long m_count;
    int m_today;
    int m_log_buf_size;
    bool m_is_async;
    char *m_buf;
    int m_split_lines;
    char log_name[128]; // log�ļ���
    char dir_name[128]; // ·����
    locker m_mutex;
    FILE *m_fp;         //��log���ļ�ָ��
    block_queue<string> *m_log_queue; //��������

private:
    void *async_write_log()
    {
        string single_log;
        // ������������ȡ��һ����־string, д���ļ���
        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

    Log()
    {
        m_count = 0;
        m_is_async = false;
    }

    ~Log()
    {
        if(m_fp != NULL)
        {
            fclose(m_fp);
        }
    }
};

#define LOG_DEBUG(format, ...)  Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)  Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
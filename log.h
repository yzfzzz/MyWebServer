#pragma once
#include "block_queue.h"
#include <string.h>
#include <sys/time.h>
#include "locker.h"
using namespace std;

class Log
{
public:
    // 返回log对象的指针
    static Log *get_instance()
    {
        /* 
            instance只能初始化一次，所以即便调用 get_instance() 函数多次，
            代码static Log instance;只执行一次，即 instance 的地址不会被改变 
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
    char log_name[128]; // log文件名
    char dir_name[128]; // 路径名
    locker m_mutex;
    FILE *m_fp;         //打开log的文件指针
    block_queue<string> *m_log_queue; //阻塞队列

private:
    void *async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一个日志string, 写入文件、
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
#include "log.h"
#include <stdarg.h>
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了max_queue_size, 则设置为异步
    if(max_queue_size >= 1)
    {
        // 设为异步模式
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        // 创建一个写线程
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    // 清空缓冲区数据
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    // 查找字符串中最后一次出现指定字符('/')的位置, 返回指向该字符的指针
    const char* p = strrchr(file_name, '/');

    char log_full_name[256] = {0};

    // 未能找到指定字符 '/'，则返回NULL
    if(p == NULL)
    {
        // 用于将格式化的数据写入到一个字符串中
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon+1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 将源字符串p+1（包括空终止符 '\0'）复制到目标字符串log_name数组中
        strcpy(log_name, p+1);
        // ?
        strncpy(dir_name, file_name, p-file_name+1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon+1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a"); 

    if (m_fp == NULL)
    {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    /*
        struct timeval {
            time_t      tv_sec;     // 秒
            suseconds_t  tv_usec;    // 微秒
        }
    */
   struct timeval now = {0,0};
    // 获取当前的日期和时间, 存储在now中
   gettimeofday(&now, NULL);
   time_t t = now.tv_sec;
   // 将秒数转换为本地时间表示形式
   struct tm *sys_tm = localtime(&t);
   struct tm my_tm = *sys_tm;
   char s[16] = {0};
   switch(level)
   {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
   }

   // 写入一个log, 对 m_count++, m_split_lines最大行数
   m_mutex.lock();
   m_count++;

   // 一天一个log，或者当文件行数满了，就新开一个log
   if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
   {
        char new_log[256] = {0};
        // 刷新由 fopen 打开的输出流（如文件或控制台）的缓冲区。当使用 fflush 刷新流时，所有缓冲中的数据将被写入到流所关联的文件或设备中。
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            // ?
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count/m_split_lines);
        }
        m_fp = fopen(new_log, "a");
   }
    m_mutex.unlock();
    // 构建一个可变参数列表
    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入具体的时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s  ", my_tm.tm_year+1900, my_tm.tm_mon+1,
                    my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // ?
    int m = vsnprintf(m_buf+n, m_log_buf_size-1, format, valst);
    m_buf[n+m] = '\n';
    // 加 \0 表示前面的字符组成一句话
    m_buf[n+m+1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
    if(m_is_async && !m_log_queue->full())
    {
        // 异步
        m_log_queue->push(log_str);
    }
    else
    {
        // 同步
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    // ?强制刷新写入流缓冲区
    // 清除读写缓冲区，强制将缓冲区内的数据写入到文件或输出流中
    fflush(m_fp);
    m_mutex.unlock();
}
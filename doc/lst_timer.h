#pragma once

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include "http_conn.h"
#define BUFFER_SIZE 64
extern int setnonblocking(int fd);
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);

class http_conn;

// pipefd[0] 对应的是管道的读端
// pipefd[1] 对应的是管道的写端
extern int pipefd[2];

// 定时器类
class util_timer{
public:
    // 构造函数
    util_timer(): prev(NULL), next(NULL) {}

public:
    time_t expire; // 任务超时时间，这里是使用绝对时间
    void (*cb_func)(http_conn*, int*);  // 任务回调函数， 回调函数处理的客户数据，由定时器的执行者传递给回调函数
    http_conn* user_data;
    util_timer* prev;
    util_timer* next;
};

// 定时器链表, 是升序的双向链表, 带有头节点和尾节点
class sort_timer_lst{
public:
    sort_timer_lst();
    // 析构函数
    ~sort_timer_lst();

    // 将目标定时器放在链表中
    void add_timer(util_timer* timer);

    // 将定时器 timer 从链表中删除
    void del_timer(util_timer* timer);

    // 当某个定时任务发生变化, 调整该定时器在链表中的位置，此函数只考虑定时时间延长的情况，即定时器往链表的后面移动
    void adjust_timer(util_timer* timer);

    /* SIGALARM每触发一次就在信号处理函数执行一次tick函数, 以处理链表上的到期任务 */
    void tick(int& epollfd);

    


    util_timer* head;
    util_timer* tail;
private:

    // 将目标定时器 timer 添加到节点 lst_head 之后的部分链表中
    void add_timer(util_timer* timer, util_timer* lst_head)
    {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;

        while(tmp)
        {
            if(timer->expire < tmp->expire)
            {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }

        // timer->expire 是最大的，则插入到末尾
        if(!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }
};

void lst_sig_init(int& epollfd);
void lst_addsig(int sig);
void lst_sig_handler(int sig);
void cb_func(http_conn* user_data, int* p_epollfd);
void lsttimer_init(http_conn* user_ptr, int timeslot,sort_timer_lst& timer_lst);
void lsttimer_handler(int& epollfd, int timeslot,sort_timer_lst& timer_lst);

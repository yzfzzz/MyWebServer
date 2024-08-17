#include "lst_timer.h"
#include <signal.h>
#include <errno.h>
#include <cassert>
sort_timer_lst::sort_timer_lst(): head(NULL), tail(NULL){}
// 析构函数
sort_timer_lst::~sort_timer_lst()
{
    util_timer* tmp = head;
    while(tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 将目标定时器放在链表中
void sort_timer_lst::add_timer(util_timer* timer)
{
    if(!timer)
    {
        return;
    }
    if(!head)
    {
        head = tail = timer;
        return;
    }
    /* 
        如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，
        则把该定时器插入链表头部,作为链表新的头节点，否则就需要调用
        重载函数 add_timer(),把它插入链表中合适的位置，以保证链表的升序特性 
    */
    if(timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

// 将定时器 timer 从链表中删除
void sort_timer_lst::del_timer(util_timer* timer)
{
    if(!timer)
    {
        return;
    }
    // 链表中只有一个定时器
    if((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 链表至少有一个定时器, 且头节点恰好是目标定时器
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 链表至少有一个定时器, 且尾节点恰好是目标定时器
    if(timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 链表至少有一个定时器, 目标定时器处在链表中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 当某个定时任务发生变化, 调整该定时器在链表中的位置，此函数只考虑定时时间延长的情况，即定时器往链表的后面移动
void sort_timer_lst::adjust_timer(util_timer* timer)
{
    if(!timer)
    {
        return;
    }
    util_timer* tmp = timer->next;
    // 目标定时器在链表的后面，或者定时时长小于后面的，则不动
    if(!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 如果目标定时器是头节点
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 目标定时器在链表中间，则重新插入到链表中
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

/* SIGALARM每触发一次就在信号处理函数执行一次tick函数, 以处理链表上的到期任务 */
void sort_timer_lst::tick()
{
    if(!head)
    {
        return;
    }
    printf( "timer tick\n" );
    time_t cur = time(NULL); // 获取当前系统的时间
    util_timer* tmp = head;
    // 从头节点依次处理每一个定时器，直到遇到一个尚未定期的定时器
    while(tmp)
    {
        // 每个定时器存的都是绝对时间
        if(cur < tmp->expire)
        {
            break;
        }
        // 调用定时器回调函数，执行定时任务
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if(head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
        printf("close client request\n");
    }
}
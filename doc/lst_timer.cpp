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
void sort_timer_lst::tick(int& epollfd)
{
    printf("In tick\n");
    if(!head)
    {
        return;
    }
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
        tmp->cb_func(tmp->user_data, &epollfd);
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


void sort_timer_lst::check()
{
    util_timer* cur = head;
    while(cur != NULL)
    {
        printf("util_timer sockfd: %d\n", cur->user_data.m_sockfd);
        cur = cur->next;
    }
}

// 信号的中断处理函数
void lst_sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;

}

// 注册信号, 添加信号中断
void lst_addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = lst_sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 信号的初始化
void lst_sig_init(int& epollfd)
{
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    printf("1-> pipefd[0]:%d, pipefd[1]:%d\n",pipefd[0],pipefd[1]);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);
    printf("2-> pipefd[0]:%d, pipefd[1]:%d\n",pipefd[0],pipefd[1]);

    lst_addsig(SIGALRM);
    lst_addsig(SIGTERM);
    printf("3-> pipefd[0]:%d, pipefd[1]:%d\n",pipefd[0],pipefd[1]);

}

// 定时器回调函数, 从epoll上删除sockfd
void cb_func(http_conn user_data, int* p_epollfd)
{
    printf("close fd : %d\n", user_data.m_sockfd);
    epoll_ctl(*p_epollfd, EPOLL_CTL_DEL, user_data.m_sockfd, 0);
    assert(&user_data);
    close(user_data.m_sockfd);
}

void lsttimer_init(http_conn user_data, int timeslot,sort_timer_lst& timer_lst)
{
    util_timer* timer = new util_timer;
    timer->user_data = user_data;
    timer->cb_func = cb_func;
    printf("userdata fd: %d\n", timer->user_data.m_sockfd);
    time_t cur = time(NULL);
    timer->expire = cur + 3*timeslot;
    user_data.timer = timer;
    timer_lst.add_timer(timer);
}

void lsttimer_handler(int& epollfd, int timeslot,sort_timer_lst& timer_lst)
{

    timer_lst.tick(epollfd);
    alarm(timeslot);

}
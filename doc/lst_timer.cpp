#include "lst_timer.h"
#include <signal.h>
#include <errno.h>
#include <cassert>
sort_timer_lst::sort_timer_lst(): head(NULL), tail(NULL){}
// ��������
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

// ��Ŀ�궨ʱ������������
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
        ���Ŀ�궨ʱ���ĳ�ʱʱ��С�ڵ�ǰ���������ж�ʱ���ĳ�ʱʱ�䣬
        ��Ѹö�ʱ����������ͷ��,��Ϊ�����µ�ͷ�ڵ㣬�������Ҫ����
        ���غ��� add_timer(),�������������к��ʵ�λ�ã��Ա�֤������������� 
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

// ����ʱ�� timer ��������ɾ��
void sort_timer_lst::del_timer(util_timer* timer)
{
    if(!timer)
    {
        return;
    }
    // ������ֻ��һ����ʱ��
    if((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // ����������һ����ʱ��, ��ͷ�ڵ�ǡ����Ŀ�궨ʱ��
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // ����������һ����ʱ��, ��β�ڵ�ǡ����Ŀ�궨ʱ��
    if(timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // ����������һ����ʱ��, Ŀ�궨ʱ�����������м�
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// ��ĳ����ʱ�������仯, �����ö�ʱ���������е�λ�ã��˺���ֻ���Ƕ�ʱʱ���ӳ������������ʱ��������ĺ����ƶ�
void sort_timer_lst::adjust_timer(util_timer* timer)
{
    if(!timer)
    {
        return;
    }
    util_timer* tmp = timer->next;
    // Ŀ�궨ʱ��������ĺ��棬���߶�ʱʱ��С�ں���ģ��򲻶�
    if(!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // ���Ŀ�궨ʱ����ͷ�ڵ�
    if(timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // Ŀ�궨ʱ���������м䣬�����²��뵽������
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

/* SIGALARMÿ����һ�ξ����źŴ�����ִ��һ��tick����, �Դ��������ϵĵ������� */
void sort_timer_lst::tick(int& epollfd)
{
    printf("In tick\n");
    if(!head)
    {
        return;
    }
    time_t cur = time(NULL); // ��ȡ��ǰϵͳ��ʱ��
    util_timer* tmp = head;
    // ��ͷ�ڵ����δ���ÿһ����ʱ����ֱ������һ����δ���ڵĶ�ʱ��
    while(tmp)
    {
        // ÿ����ʱ����Ķ��Ǿ���ʱ��
        if(cur < tmp->expire)
        {
            break;
        }
        // ���ö�ʱ���ص�������ִ�ж�ʱ����
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

// �źŵ��жϴ�����
void lst_sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;

}

// ע���ź�, ����ź��ж�
void lst_addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = lst_sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// �źŵĳ�ʼ��
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

// ��ʱ���ص�����, ��epoll��ɾ��sockfd
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
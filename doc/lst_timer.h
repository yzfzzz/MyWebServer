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

// pipefd[0] ��Ӧ���ǹܵ��Ķ���
// pipefd[1] ��Ӧ���ǹܵ���д��
extern int pipefd[2];

// ��ʱ����
class util_timer{
public:
    // ���캯��
    util_timer(): prev(NULL), next(NULL) {}

public:
    time_t expire; // ����ʱʱ�䣬������ʹ�þ���ʱ��
    void (*cb_func)(http_conn*, int*);  // ����ص������� �ص���������Ŀͻ����ݣ��ɶ�ʱ����ִ���ߴ��ݸ��ص�����
    http_conn* user_data;
    util_timer* prev;
    util_timer* next;
};

// ��ʱ������, �������˫������, ����ͷ�ڵ��β�ڵ�
class sort_timer_lst{
public:
    sort_timer_lst();
    // ��������
    ~sort_timer_lst();

    // ��Ŀ�궨ʱ������������
    void add_timer(util_timer* timer);

    // ����ʱ�� timer ��������ɾ��
    void del_timer(util_timer* timer);

    // ��ĳ����ʱ�������仯, �����ö�ʱ���������е�λ�ã��˺���ֻ���Ƕ�ʱʱ���ӳ������������ʱ��������ĺ����ƶ�
    void adjust_timer(util_timer* timer);

    /* SIGALARMÿ����һ�ξ����źŴ�����ִ��һ��tick����, �Դ��������ϵĵ������� */
    void tick(int& epollfd);

    


    util_timer* head;
    util_timer* tail;
private:

    // ��Ŀ�궨ʱ�� timer ��ӵ��ڵ� lst_head ֮��Ĳ���������
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

        // timer->expire �����ģ�����뵽ĩβ
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

#pragma once
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#define BUFFER_SIZE 64

class util_timer;
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// ��ʱ����
class util_timer{
public:
    // ���캯��
    util_timer(): prev(NULL), next(NULL) {}

public:
    time_t expire; // ����ʱʱ�䣬������ʹ�þ���ʱ��
    void (*cb_func)(client_data*);  // ����ص������� �ص���������Ŀͻ����ݣ��ɶ�ʱ����ִ���ߴ��ݸ��ص�����
    client_data* user_data;
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
    void tick();

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
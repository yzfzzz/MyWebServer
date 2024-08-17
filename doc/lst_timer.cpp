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
void sort_timer_lst::tick()
{
    if(!head)
    {
        return;
    }
    printf( "timer tick\n" );
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
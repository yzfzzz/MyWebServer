#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "threadpool.hpp"
#include "locker.h"
#include "http_conn.h"
#include <signal.h>
#include <assert.h>  
#include "lst_timer.h"
#include "log.h"
#define MAX_FD 65536  // �����ļ�������
#define MAX_EVENT_NUMBER 10000 // ����������¼���
#define TIMESLOT 5

//���ö�ʱ����ز���
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;
// �źŵ��жϴ�����
void timer_sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}
// ��ʱ���ص�����, ��epoll��ɾ��sockfd
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(&user_data);
    close(user_data->sockfd);
#ifdef ASYNLOG
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
#endif
    // printf("close fd %d\n", user_data->sockfd);
}
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}
/* 
    ����ָ�������: ����˵���� (*������) (����)
    void(handler)(int) ������һ����Ϊ handler �ĺ���ָ�룬��ָ��һ������һ�� int ���������� void �ĺ���
*/
void addsig(int sig, void(handler)(int), bool restart = false)
{
    // sigaction���������
    struct sigaction sa;
    // ָ��sa�ڴ������ǰn���ֽڶ�����Ϊĳ���ض���ֵ('\0')�����ڶ��·�����ڴ���г�ʼ��
    memset(&sa, '\0', sizeof(sa));
    // д�뺯��ָ�룬ָ��ĺ��������źŲ�׽��֮��Ĵ�����
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    // ������ʱ�����źż�
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// �źŵĳ�ʼ��
void timer_sig_init()
{
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);
    printf("pipefd0: %d, pipefd1: %d\n", pipefd[0], pipefd[1]);
    addsig(SIGALRM, timer_sig_handler);
    addsig(SIGTERM, timer_sig_handler);
}

int main(int argc, char* argv[])
{
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 5000000, 8);
#endif
    if(argc <= 1)
    {
        // Ҫ�������ʽΪ ./a.out 10000  ����10000�Ƕ˿ں� 
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // �˿ں� string -> int
    int port = atoi(argv[1]);
    // �����һ��û�ж��˵Ĺܵ�д���ݣ�������ֹ����
    addsig(SIGPIPE, SIG_IGN);   // SIG_IGN: �����źţ�����ָ���Ǻ����ź� ��  SIGPIPE

    // ����һ���̳߳�ָ��
    threadpool<http_conn>* pool = NULL;
    try {
        // ����һ���̳߳�
        pool = new threadpool<http_conn>;
    }catch(...)
    {
        // ���쳣���˳�
        return 1;
    }
    // ����һ��������http_conn���飬���������������ӵĿͻ�����Ϣ
    http_conn* users = new http_conn[MAX_FD];
    client_data *users_timer = new client_data[MAX_FD];

    // ���ü���
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    printf("listen fd: %d\n", listenfd);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // ���ö˿ڸ���
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // ��
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1)
    {
        perror("bind");
        exit(-1);
    }

    // ��ʼ����
    ret = listen(listenfd, 5);
    if(ret == -1)
    {
        perror("listen");
        exit(-1);
    }
    
    // ��listend��ӵ�epollģ����
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    timer_sig_init();

    bool timeout = false;
    bool stop_server = false;
    alarm(TIMESLOT);
    while(!stop_server)
    {
        // epoll��ѯ���ȴ������ݷ���
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER,-1);
        // printf("epoll num: %d\n", number);
        if((number < 0) && (errno != EINTR))
        {
            // printf("epoll failture\n");
        #ifdef ASYNLOG
            LOG_ERROR("%s", "epoll failure");
        #endif
            break;
        }
        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            struct sockaddr_in client_address;
            socklen_t client_addresslen = sizeof(client_address);
            int connfd;
            // ���µĿͻ�������
            if(sockfd == listenfd)
            {
                while ((connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslen)) > 0)
                {

                    // printf("connfd: %d\n", connfd);
                #ifdef ASYNLOG
                    LOG_INFO("connfd: %d", connfd);
                    Log::get_instance()->flush();
                #endif
                    if(connfd < 0)
                    {
                        // printf("errno is %d\n", errno);
                    #ifdef ASYNLOG
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    #endif
                        continue;
                    }

                    if(http_conn::m_user_count >= MAX_FD)
                    {
                        close(connfd);
                    #ifdef ASYNLOG
                        LOG_ERROR("%s", "Internal server busy");
                    #endif
                        continue;
                    }
                    users[connfd].init(connfd, client_address);
                    //��ʼ��client_data����
                    //������ʱ�������ûص������ͳ�ʱʱ�䣬���û����ݣ�����ʱ����ӵ�������
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3*TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
            }
            else if((sockfd == pipefd[0])&&(events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }

            // ���Է��쳣�˿������
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // users[sockfd].close_conn();

                //�������˹ر����ӣ��Ƴ���Ӧ�Ķ�ʱ��
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }
            // �ж��¼��������ɶ���
            else if(events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;
                // �ж��¼�����
                if(users[sockfd].read())
                {
                    // ���ĵ�����
                    pool->append(users+sockfd);
                #ifdef ASYNLOG
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                #endif
                    //�������ݴ��䣬�򽫶�ʱ�������ӳ�3����λ
                    //�����µĶ�ʱ���������ϵ�λ�ý��е���
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    #ifdef ASYNLOG
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                    #endif
                    }
                }
                else
                {
                    // ����������
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                    // users[sockfd].close_conn();

                }
            }
            // ��д�¼���������д��
            else if(events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if(users[sockfd].write())
                {
                #ifdef ASYNLOG  
                    LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                #endif
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                    #ifdef ASYNLOG 
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                    #endif
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                    // users[sockfd].close_conn();
                }
            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }

    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
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
#define MAX_FD 65536  // 最大的文件描述符
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数
#define TIMESLOT 5

//设置定时器相关参数
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;
// 信号的中断处理函数
void timer_sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}
// 定时器回调函数, 从epoll上删除sockfd
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
    函数指针的声明: 类型说明符 (*函数名) (参数)
    void(handler)(int) 声明了一个名为 handler 的函数指针，它指向一个接受一个 int 参数并返回 void 的函数
*/
void addsig(int sig, void(handler)(int), bool restart = false)
{
    // sigaction的输入参数
    struct sigaction sa;
    // 指定sa内存区域的前n个字节都设置为某个特定的值('\0')，用于对新分配的内存进行初始化
    memset(&sa, '\0', sizeof(sa));
    // 写入函数指针，指向的函数就是信号捕捉到之后的处理函数
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    // 设置临时阻塞信号集
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 信号的初始化
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
        // 要求输入格式为 ./a.out 10000  其中10000是端口号 
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // 端口号 string -> int
    int port = atoi(argv[1]);
    // 如果向一个没有读端的管道写数据，不用终止进程
    addsig(SIGPIPE, SIG_IGN);   // SIG_IGN: 忽略信号，这里指的是忽略信号 ·  SIGPIPE

    // 定义一个线程池指针
    threadpool<http_conn>* pool = NULL;
    try {
        // 开辟一个线程池
        pool = new threadpool<http_conn>;
    }catch(...)
    {
        // 若异常则退出
        return 1;
    }
    // 开辟一块连续的http_conn数组，保存所有正在连接的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    client_data *users_timer = new client_data[MAX_FD];

    // 设置监听
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    printf("listen fd: %d\n", listenfd);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1)
    {
        perror("bind");
        exit(-1);
    }

    // 开始监听
    ret = listen(listenfd, 5);
    if(ret == -1)
    {
        perror("listen");
        exit(-1);
    }
    
    // 将listend添加到epoll模型中
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
        // epoll轮询，等待有数据发送
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
            // 有新的客户端连接
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
                    //初始化client_data数据
                    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
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

            // 若对方异常端开或错误
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // users[sockfd].close_conn();

                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }
            // 有读事件发生（可读）
            else if(events[i].events & EPOLLIN)
            {
                util_timer *timer = users_timer[sockfd].timer;
                // 有读事件发生
                if(users[sockfd].read())
                {
                    // 读的到数据
                    pool->append(users+sockfd);
                #ifdef ASYNLOG
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                #endif
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
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
                    // 读不到数据
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                    // users[sockfd].close_conn();

                }
            }
            // 有写事件发生（可写）
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
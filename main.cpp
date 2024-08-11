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

#define MAX_FD 65536  // 最大的文件描述符
#define MAX_EVENT_NUMBER 10000 // 监听的最大事件数

/* 
    函数指针的声明: int (*pfun)(int,int); //声明一个函数指针 pfun,参数列表为 int,int
    void(handler)(int) 声明了一个名为 handler 的函数指针，它指向一个接受一个 int 参数并返回 void（即不返回任何值）的函数
*/
void addsig(int sig, void(handler)(int))
{
    // sigaction的输入参数
    struct sigaction sa;
    // 指定sa内存区域的前n个字节都设置为某个特定的值('\0')，用于对新分配的内存进行初始化
    memset(&sa, '\0', sizeof(sa));
    // 写入函数指针，指向的函数就是信号捕捉到之后的处理函数
    sa.sa_handler = handler;
    // 设置临时阻塞信号集
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        // 要求输入格式为 ./a.out 10000  其中10000是端口号 
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // 端口号string -> int
    int port = atoi(argv[1]);
    // 如果向一个没有读端的管道写数据，不用终止进程
    addsig(SIGPIPE, SIG_IGN);   // SIG_IGN: 忽略信号

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
    // 监听
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1)
    {
        perror("bind");
        exit(-1);
    }
    ret = listen(listenfd, 5);
    if(ret == -1)
    {
        perror("listen");
        exit(-1);
    }
    
    // 添加到epoll模型中
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    while(1)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER,-1);
        if((number < 0) && (errno != EINTR))
        {
            printf("epoll failture\n");
            break;
        }
        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addresslen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addresslen);

                if(connfd < 0)
                {
                    printf("errno is %d\n", errno);
                    continue;
                }

                if(http_conn::m_user_count >= MAX_FD)
                {
                    close(connfd);
                    continue;
                }
                users[connfd].init(connfd, client_address);
            }

            // 若对方异常端开或错误
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                users[sockfd].close_conn();
            }
            // 有读事件发生
            else if(events[i].events & EPOLLIN)
            {
                // 有读事件发生
                if(users[sockfd].read())
                {
                    // 读的到数据
                    pool->append(users+sockfd);
                }
                else
                {
                    // 读不到数据
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                if(!users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }
        }

    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}
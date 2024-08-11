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

#define MAX_FD 65536  // �����ļ�������
#define MAX_EVENT_NUMBER 10000 // ����������¼���

/* 
    ����ָ�������: int (*pfun)(int,int); //����һ������ָ�� pfun,�����б�Ϊ int,int
    void(handler)(int) ������һ����Ϊ handler �ĺ���ָ�룬��ָ��һ������һ�� int ���������� void�����������κ�ֵ���ĺ���
*/
void addsig(int sig, void(handler)(int))
{
    // sigaction���������
    struct sigaction sa;
    // ָ��sa�ڴ������ǰn���ֽڶ�����Ϊĳ���ض���ֵ('\0')�����ڶ��·�����ڴ���г�ʼ��
    memset(&sa, '\0', sizeof(sa));
    // д�뺯��ָ�룬ָ��ĺ��������źŲ�׽��֮��Ĵ�����
    sa.sa_handler = handler;
    // ������ʱ�����źż�
    sigfillset(&sa.sa_mask);

    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        // Ҫ�������ʽΪ ./a.out 10000  ����10000�Ƕ˿ں� 
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // �˿ں�string -> int
    int port = atoi(argv[1]);
    // �����һ��û�ж��˵Ĺܵ�д���ݣ�������ֹ����
    addsig(SIGPIPE, SIG_IGN);   // SIG_IGN: �����ź�

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
    // ����
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // ���ö˿ڸ���
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
    
    // ��ӵ�epollģ����
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

            // ���Է��쳣�˿������
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                users[sockfd].close_conn();
            }
            // �ж��¼�����
            else if(events[i].events & EPOLLIN)
            {
                // �ж��¼�����
                if(users[sockfd].read())
                {
                    // ���ĵ�����
                    pool->append(users+sockfd);
                }
                else
                {
                    // ����������
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
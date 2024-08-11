#pragma once
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstdarg>
#include <sys/types.h>  
#include <sys/uio.h>

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

class http_conn{
public:
    // 
    static int m_epollfd;
    static int m_user_count;
    static const int FILENAME_LEN = 200;  // �ļ�������󳤶�
    static const  int READ_BUFFER_SIZE = 2048;
    static const  int WRITE_BUFFER_SIZE = 1024;


    // ��������
    char m_read_buf[READ_BUFFER_SIZE];
    // ��Ƕ�����������λ�õ��׵�ַ
    int m_read_idx;
    // д������
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    // HTTP���󷽷�������ֻ֧��GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    /*
    �����ͻ�������ʱ����״̬����״̬
    CHECK_STATE_REQUESTLINE:��ǰ���ڷ���������
    CHECK_STATE_HEADER:��ǰ���ڷ���ͷ���ֶ�
    CHECK_STATE_CONTENT:��ǰ���ڽ���������
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT };
    /*
    ����������HTTP����Ŀ��ܽ�������Ľ����Ľ��
    NO_REQUEST : ������������Ҫ������ȡ�ͻ�����
    GET_REQUEST : ��ʾ�����һ����ɵĿͻ�����
    BAD_REQUEST : ��ʾ�ͻ������﷨����
    NO_RESOURCE : ��ʾ������û����Դ
    FORBIDDEN_REQUEST : ��ʾ�ͻ�����Դû���㹻�ķ���Ȩ��
    FILE_REQUEST : �ļ�����,��ȡ�ļ��ɹ�
    INTERNAL_ERROR : ��ʾ�������ڲ�����
    CLOSED_CONNECTION : ��ʾ�ͻ����Ѿ��ر�������
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    // ��״̬�������ֿ���״̬�����еĶ�ȡ״̬���ֱ��ʾ
    // 1.��ȡ��һ���������� 2.�г��� 3.���������Ҳ�����
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}
    void init(int sockfd, const sockaddr_in& addr);  // sockaddr_in&: ����
    // �ر�����
    void close_conn();
    // ��������
    bool read();
    // ������д
    bool write();
    void process();

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* getline(){return m_read_buf + m_start_line;}
    void unmap();
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    bool add_content_type();
    bool add_content( const char* content );

private:
    int m_sockfd;
    int m_checked_idx;  // ��ǰ���ڷ������ַ��ڶ��������е�λ��
    sockaddr_in m_address;
    CHECK_STATE m_check_state;
    int m_start_line;           // ��ǰ���ڽ�������ʼ��ַ
    char* m_url; // �ͻ������Ŀ���ļ����ļ���
    METHOD m_method; // ���󷽷�
    char* m_version; // HTTPЭ��汾�ţ����ǽ�֧��HTTP1.1
    int m_content_length;
    bool m_linger; // HTTP�����Ƿ�Ҫ�󱣳�����
    char* m_host; // ������
    char m_real_file[FILENAME_LEN];  // ����·��
    struct stat m_file_stat; // Ŀ���ļ���״̬��ͨ�������ǿ����ж��ļ��Ƿ���ڡ��Ƿ�ΪĿ¼���Ƿ�ɶ�������ȡ�ļ���С����Ϣ
    char* m_file_address; // �ͻ������Ŀ���ļ���mmap���ڴ��е���ʼλ��
    struct iovec m_iv[2];  // �Ӷ����ͬ(������)���ڴ������ռ����ݷ��͵�һ�������׽���
    int m_iv_count;
    int bytes_to_send; // ��Ҫ���͵����ݵ��ֽ���
    int bytes_have_send; // �Ѿ����͵��ֽ���
};

void addfd(int epollfd, int fd, bool one_shot);
void removefd(int epollfd, int fd);
int setnonblocking(int fd);
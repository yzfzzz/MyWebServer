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
    static const int FILENAME_LEN = 200;  // 文件名的最大长度
    static const  int READ_BUFFER_SIZE = 2048;
    static const  int WRITE_BUFFER_SIZE = 1024;


    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 标记读缓冲区空余位置的首地址
    int m_read_idx;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    /*
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE:当前正在分析请求行
    CHECK_STATE_HEADER:当前正在分析头部字段
    CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT };
    /*
    服务器处理HTTP请求的可能结果，报文解析的结果
    NO_REQUEST : 请求不完整，需要继续读取客户数据
    GET_REQUEST : 表示获得了一个完成的客户请求
    BAD_REQUEST : 表示客户请求语法错误
    NO_RESOURCE : 表示服务器没有资源
    FORBIDDEN_REQUEST : 表示客户对资源没有足够的访问权限
    FILE_REQUEST : 文件请求,获取文件成功
    INTERNAL_ERROR : 表示服务器内部错误
    CLOSED_CONNECTION : 表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}
    void init(int sockfd, const sockaddr_in& addr);  // sockaddr_in&: 引用
    // 关闭连接
    void close_conn();
    // 非阻塞读
    bool read();
    // 非阻塞写
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
    int m_checked_idx;  // 当前正在分析的字符在读缓冲区中的位置
    sockaddr_in m_address;
    CHECK_STATE m_check_state;
    int m_start_line;           // 当前正在解析的起始地址
    char* m_url; // 客户请求的目标文件的文件名
    METHOD m_method; // 请求方法
    char* m_version; // HTTP协议版本号，我们仅支持HTTP1.1
    int m_content_length;
    bool m_linger; // HTTP请求是否要求保持连接
    char* m_host; // 主机名
    char m_real_file[FILENAME_LEN];  // 本地路径
    struct stat m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    char* m_file_address; // 客户请求的目标文件被mmap到内存中的起始位置
    struct iovec m_iv[2];  // 从多个不同(不连续)的内存区域收集数据发送到一个网络套接字
    int m_iv_count;
    int bytes_to_send; // 将要发送的数据的字节数
    int bytes_have_send; // 已经发送的字节数
};

void addfd(int epollfd, int fd, bool one_shot);
void removefd(int epollfd, int fd);
int setnonblocking(int fd);
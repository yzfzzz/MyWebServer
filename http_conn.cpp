#include "http_conn.h"

// ����HTTP��Ӧ��һЩ״̬��Ϣ
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherentlyimpossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from thisserver.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on thisserver.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requestedfile.\n";

// ��վ�ĸ�Ŀ¼
const char* doc_root = "/home/My_WebServer/resources";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// ���ͻ��˵�socket_fd���뵽epoll_fd��
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // event.events��32λ������, EPOLLIN/EPOLLRDHUP��ʾ��32bit�Ķ������ϣ�ֻ��1bit��1�����඼��0
    // event.events = EPOLLIN | EPOLLRDHUP;

    // ��Ϊ���ش���
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    // ���ø�fdֻ�ܽ���һ�����ݣ����������Ҫ����modfd()����
    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // �����ļ�������Ϊ������
    setnonblocking(fd);
}

// ����socket�ϵ��¼�����ע�ᵽͬһ��epoll�ں��¼��У��������óɾ�̬��
int http_conn::m_epollfd = -1;
// ���еĿͻ���
int http_conn::m_user_count = 0;

// ���ͻ��˵�socket_fd��epoll_fd���Ƴ�
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// ���ø�fdֻ�ܽ���һ�����ݣ����������Ҫ����modfd()����
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// http_conn��ĳ�ʼ��, ʵ�����ǿͻ���socket_fd�ĳ�ʼ��
void http_conn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    // һ���������Ը�ֵ��һ������
    m_address = addr;
    // ���ö˿ڸ���
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

// http_conn�������Ա�����ĳ�ʼ��
void http_conn::init()
{
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;    // ��ʼ״̬Ϊ���������
    m_linger = false;                           // Ĭ�ϲ��������� Connection : keep-alive��������
    m_method = GET;                             // Ĭ������ʽΪGET
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    // !�ڴ��ַ�ļ���: ��Ա�����Ĵ洢ʱ��������
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

void http_conn::close_conn()
{
    if(m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // �ر�һ�����ӣ��ͻ�����-1
    }
}

bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
    {
        // ����������, Ӧ�ò㻹δ����û���������
        return false;
    }
    int byte_read = 0;
    while(1)
    {
        
        byte_read = recv(m_sockfd, m_read_buf+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
        if(byte_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // û������
                break;
            }
            
            return false;
        }
        else if(byte_read == 0)
        {
            // �Է��ر�����
            return false;
        }

        m_read_idx += byte_read;
    }
    
    // printf("%s\n", m_read_buf);

    return true;
}

// ����ڴ�ӳ��
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    if(bytes_to_send == 0)
    {
        // ��Ҫ���͵��ֽ�Ϊ0��������Ӧ����
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while(1)
    {
        // ��ɢд
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1)
        {
            // ���tcpû��д����Ŀռ䣬��ȴ���һ�ֿ�д�¼�
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        // ����m_iv[0]��
        if(bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len  = 0;
            m_iv[1].iov_base = m_file_address +(bytes_have_send-m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        // ����m_iv[0]�Ĳ������ݣ�����һ����û��
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if(bytes_to_send <= 0)
        {
            // û������Ҫ������
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

// Ԥ����, ������һ��һ��, �ж�����: \r\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(;m_checked_idx < m_read_idx; m_checked_idx++)
    {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r')
        {
            if((m_checked_idx+1) == m_read_idx)
            {
                // ���ݲ�����
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx+1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';  // �ȱ��'\0', ��+1
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // ��һ��ĩβ��'\r', ��һ�ο�ͷ��'\n'
        else if(temp == '\n')
        {
            if((m_checked_idx > 1)&&(m_read_buf[m_checked_idx-1]=='\r'))
            {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// �������н��д���, ��ȡ���󷽷���Ŀ��URL��HTTP�汾��
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    // ��Ҫ����������������: GET /index.html HTTP/1.1
    m_url = strpbrk(text, " ");    // �ҳ���һ�� " "��text���ֵ�λ��
    if(!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';    // m_url ��ǰָ��ĵ�ַ�������ã�*m_url����Ȼ�����λ�õ��ַ�����Ϊ '\0'��Ȼ�󣬽� m_url ������ʹ��ָ����һ���ַ���λ�á�
    // ��ǰ����������: GET\0/index.html HTTP/1.1
    char* method = text;
    if(strcasecmp(method, "GET") == 0)
    {
        // ���Դ�Сд�Ƚ�
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    // ��ʱm_url����++��ָ�� /index.html HTTP/1.1
    m_version = strpbrk(m_url, " ");
    if(!m_version)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0';
    // m_versionָ�� HTTP/1.1
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // ��Щ������ /index.html, ���� http://192.168.110.129:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // m_urlָ�� 192.168.110.129:10000/index.html
        m_url = strchr(m_url, '/'); // s�����ַ����е�һ�γ��� '/�� ��λ��
    }
    // m_urlָ�� /index.html
    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    // ���״̬��ɼ��ͷ
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    // ��Ϊparse_line()Ԥ����ʱ���Ѿ���\r\n�滻Ϊ\0\0
    if(text[0] == '\0')
    {
        // ���HTTP��������Ϣ�壬����Ҫ��ȡm_content_length�ֽڵ���Ϣ�壬
        // ״̬��ת�Ƶ�CHECK_STATE_CONTENT״̬
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // ����õ�һ��������HTTP����
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        // ����Connection�ֶ� Connection: keep-alive
        text += 11;
        text += strspn(text, " ");  // ����text�ַ����У�ǰ׺����(' ')�ĳ���
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // ����Content-Lengthͷ���ֶ�
        text +=15;
        text += strspn(text, " ");
        m_content_length = atoi(text);
    }
    else if(strncasecmp(text, "HOST:", 5) == 0)
    {
        // ����HOSTͷ���ֶ�
        text += 5;
        text += strspn(text, " ");
        m_host = text;
    }
    else
    {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// ����HTTP�������Ϣ��
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// ���õ�һ����������ȷ��HTTP����ʱ�����Ǿͷ���Ŀ���ļ������ԣ�
// ���Ŀ���ļ����ڡ��������û��ɶ����Ҳ���Ŀ¼����ʹ��mmap����
// ӳ�䵽�ڴ��ַm_file_address���������ߵ����߻�ȡ�ļ��ɹ�
http_conn::HTTP_CODE http_conn::do_request()
{
    
    // "/home/WebServer/resources"
    strcpy(m_real_file, doc_root);  // �ڽ�һ���ַ���(doc_root)���Ƶ���һ���ַ���(m_real_file)��
    
    int len = strlen(doc_root);
    // strncpy ��Դ�ַ���m_url�������FILENAME_LEN-len-1�ַ���Ŀ���ַ���m_real_file+len�С�
    // ���m_url�ĳ���С��FILENAME_LEN-len-1����m_real_file+len��ʣ��Ĳ��ֻ��ÿ��ַ� '\0' ���
    strncpy(m_real_file+len, m_url, FILENAME_LEN-len-1);
    // ��ȡ�ļ���״̬
    if(stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    // �жϷ���Ȩ��
    if(!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // �ж��Ƿ�ΪĿ¼
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // ��ֻ���ķ�ʽ���ļ�
    int fd = open(m_real_file, O_RDONLY);
    // �����ڴ�ӳ��, �������ļ�������ӳ�䵽�ڴ棬�û�ͨ���޸��ڴ�����޸Ĵ����ļ�
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// �ͻ���һ�����󣬷������ͷ������ݸ��ͻ���
void http_conn::process()
{
    // �ͻ��˷�������, ����������http����
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // ������Ӧ���������������ݸ��ͻ���
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    // ��ʾ�����ݷ���
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// ��״̬��, ��������
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    // CHECK_STATE_CONTENT:��ǰ���ڽ���������
    while( ((m_check_state == CHECK_STATE_CONTENT)&&(line_status == LINE_OK)) ||
    ((line_status = parse_line()) == LINE_OK) )
    {
        // �ɹ�����һ��
        text = getline();

        // ������һ�ν�������ʼλ��
        m_start_line = m_checked_idx;
        // printf("got a http line: %s\n", text);

        switch(m_check_state)
        {
            // ����������
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }

            // ��������ͷ
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }

            // ������Ϣ��
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                {
                    if(ret == GET_REQUEST)
                    {
                        return do_request();
                    }
                    // �д����˳�����ѭ��
                    line_status = LINE_OPEN;
                    break;
                }
            }

            default:{
                /*
                    ָ��ϵͳ��Ӧ�ó��������ڲ������Ĵ������ִ���ͨ�������ڳ����ϵͳ�ڲ��߼����⡢���ô���
                    ������״̬����ġ����������û���������Ĵ��󣬶������ڴ����ϵͳ״̬��ȱ�������µġ�
                */
                return INTERNAL_ERROR;
            }
        }



    }
}

// !!! ��д�������з�������
// ���������е�`...`����Ϊ�ɱ�����б��ʡ�Ժţ���ʾ�ú������Խ���һ����ȷ�������Ĳ�������Щ���������ͺ������ڱ���ʱ�ǲ�ȷ����
// format ��ʾ�ַ����ĸ�ʽ
bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;  // �ɱ�����б�����
    va_start(arg_list, format);  // ������ arg_list ������ʹ��ָ�� format ����֮���λ��
    // ʹ��vsnprintf()������һ���ַ�����������ӡ��ʽ���ַ���
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE-1-m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_length)
{
    return add_response("Content-Length: %d\r\n", content_length);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true)? "keep-alive":"close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        case  FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            // iov_base ��һ��ָ�򻺳�����ָ�룬�û���������Ҫ��ȡ��д������ݡ�
            // iov_len ָ���� iov_base ָ��Ļ����������ݵĳ��ȣ����ֽ�Ϊ��λ��
            // m_iv[0]: ��Ӧ���ݱ�
            // m_iv[1]: ��ҳ����
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"

class http_conn
{
public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //请求方法，此处只支持GET
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    //解析客户请求的时候，主机状态所处的状态（状态机）
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    //处理请求后的结果
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}

public:
    //初始化新接收的连接
    void init( int sockfd, const sockaddr_in& addr );
    void close_conn( bool real_close = true );
    void process();
    bool read();
    bool write();

private:
    //初始化连接
    void init();
    //解析http请求
    HTTP_CODE process_read();
    //填充http的response
    bool process_write( HTTP_CODE ret );
    
    //以下用于解析http请求，
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

    //以下用于填充http的response
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

public:
    //所有socket事件都注册到同一个epoll事件表中
    static int m_epollfd;
    static int m_user_count;

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];
    int m_read_idx; //标志已读入客户数据的最后字节的下一位置
    int m_checked_idx; //当前分析中的字符在缓冲区的位置
    int m_start_line; //分析中的行起始位
    char m_write_buf[ WRITE_BUFFER_SIZE ];
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    char m_real_file[ FILENAME_LEN ]; //文件的完整路径
    char* m_url; //目标文件名
    char* m_version;
    char* m_host;
    int m_content_length;
    bool m_linger; //http请求是否要求保持连接

    //客户请求的目标文件被mmap到内存中的起始位置
    char* m_file_address;
    //标志目标文件 是否存在，是否为目录 可读以及获取文件大小
    struct stat m_file_stat;
    //以下用于writev
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif

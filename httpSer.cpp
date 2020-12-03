#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"


/*
作用：声明外部变量。使变量或对象可以被跨文件访问

c++语言支持分离式编译机制，该机制允许将程序分割为若干个文件，每个文件可被独立编译。

因此在编译期间，各个文件中定义的全局变量互相不透明，也就是说，在编译时，全局变量的可见域限制在文件内部。

对于A.cpp和B.cpp中，分别含有同名全局变量i，两个cpp文件会成功编译，但是在链接时会将两个cpp合二为一就会出现错误。这是因为同名存在重复定义。

如果在其中一个变量前添加extern关键字进行编译，再次进行链接时就会成功。完成了变量跨文件访问。
*/

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );

void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}

int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    //忽略sigpipe信号
    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while (true)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                
                users[connfd].init( connfd, client_address );
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN)
            {
                if(users[sockfd].read())
                {
                    pool->append(users + sockfd);
                }
                else
                {
                    users[sockfd].close_conn();
                }
                
            }
            else if(events[i].events & EPOLLOUT)
            {
                if(!users[sockfd].write())
                    users[sockfd].close_conn();
            }
            else
            {
                
            }
            
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}

/*
EPOLLOUT事件表示fd的发送缓冲区可写，在一次发送大量数据（超过发送缓冲区大小）的情况下很有用。
要理解该事件的意义首先要清楚一下几个知识：1、多路分离器。多路分离器存在的意义在于可以同时监测多个fd的事件，
便于单线程处理多个fd，epoll是众多多路分离器的一种，类似的还有select、poll等。
服务器程序通常需要具备较高处理用户并发的能力，使用多路分离器意味着可以用一个线程同时处理多个用户并发请求。

2、非阻塞套接字。   
 2.1 阻塞。         
 在了解非阻塞之前先了解一下阻塞，阻塞指的是用户态程序调用系统api进入内核态后，
 如果条件不满足则被加入到对应的等待队列中，直到条件满足。比如：sleep 2s。
 在此期间线程得不到CPU调度，自然也就不会往下执行，表现的现象为线程卡在系统api不返回。   
 2.2 非阻塞。         
 非阻塞则相反，不论条件是否满足都会立即返回到用户态，线程的CPU资源不会被剥夺，也就意味着程序可以继续往下执行。   
 2.3、高性能。在一次发送大量数据（超过发送缓冲区大小）的情况下，如果使用阻塞方式，程序一直阻塞，
 直到所有的数据都写入到缓冲区中。例如，要发送M字节数据，套接字发送缓冲区大小为B字节，
 只有当对端向本机返回ack表明其接收到大于等于M-B字节时，才意味着所有的数据都写入到缓冲区中。
 很明显，如果一次发送的数据量非常大，比如M=10GB、B=64KB，则：
 1）一次发送过程中本机线程会在一个fd上阻塞相当长一段时间，其他fd得不到及时处理；
 2）如果出现发送失败，无从得知到底有多少数据发送成功，应用程序只能选择重新发送这10G数据，
 结合考虑网络的稳定性，只能呵呵；总之，上述两点都是无法接受的。因此，对性能有要求的服务器一般不采用阻塞而采用非阻塞。
 
3、使用非阻塞套接字时的处理流程。    采用非阻塞套接字一次发送大量数据的流程：
1）使劲往发送缓冲区中写数据，直到返回不可写；
2）等待下一次缓冲区可写；
3）要发送的数据写完；    
 其中2）可以有两种方式：
  a）查询式，程序不停地查询是否可写；
  b）程序去干其他的事情（多路分离器的优势所在），等出现可写事件后再接着写；很明显方式b）更加优雅。

4、EPOLLOUT事件的用途。    EPOLLOUT事件就是以事件的方式通知用户程序，可以继续往缓冲区写数据了。
*/
    
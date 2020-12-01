#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024

/*
主要的信号意思：
SIGHUP 控制终端挂起
SIGPIPE 往读端被关闭的管道或者 socket连接中写数据
SIGURG socket连接上收到紧急数据
SIGALRM 定期器超时引起的
SIGCHLD 子进程状态发生变化（退出或暂停）
*/


static int pipefd[2];

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void sig_handler(int sig)
{
    //保留原来的errno 在函数最后恢复，保证函数的可重入性
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);//将信号写入管道，通知主循环
    errno = save_errno;
}

//设置信号回调的处理函数
//为信号设置了处理函数，默认情况下系统调用会被中断，
//设置SA_RESTART重启
void addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART; //重启系统调用
    sigfillset(&sa.sa_mask);//设置所有信号
    assert(sigaction(sig, &sa, NULL) != -1);
}
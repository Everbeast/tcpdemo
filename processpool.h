#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

//半同步、半异步进程池
/*
在并发模式中，同步指程序完全按照代码序列的顺序执行
            异步指程序执行需要由系统时间来驱动（如中断、信号）
在半同步半异步模式中，同步线程用于处理客户逻辑，异步线程用于处理I/O时间
过程：
    异步线程监听到客户请求后，将其封装成请求对象并插入请求队列中，
请求队列将通知某个工作在同步模式的工作线程来读取并处理这个请求对象
*/


//子进程的类，m_pid是目标子进程的PID， m_pipefd是父子进程的通信管道
class process{
public:
    process(): m_pid(-1){}
public:
    pid_t m_pid;
    int m_pipedfd[2];
};

//进程池类，模板参数是处理逻辑任务的类
template<typename T>
class processpool
{
private:
    processpool(int listenfd, int process_number = 8);
public:
    static processpool<T>* create(int listenfd, int process_number = 8){
        if(!m_instance)
        {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }

    ~processpool(){
        delete [] m_sub_process;
    }

    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    //进程池允许的最大进程数
    static const int MAX_PROCESS_NUMBER = 16;
    //每个子进程最多能处理的客户数量
    static const int USR_PER_PROCESS = 65536;
    //epoll最多能处理的事件数
    static const int MAX_EVENT_NUMBER = 10000;
    //进程池中的进程数
    int m_process_number;
    //进程在池中的序号 0开始
    int m_idx;
    //每个进程对应一个epoll内核事件表，用m_epollfd标识
    int m_epollfd;
    //监听socket
    int m_listenfd;
    //是否停止运行子进程的flag
    int m_stop;
    //保存所有子进程的描述信息
    process* m_sub_process;
    //进行池的静态实例
    static processpool<T>* m_instance;
};

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;


//用于处理信号的管道，实现统一事件源
static int sig_pipefd[2];

static int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

static void addfd(int epollfd, int fd){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从epollfd对应的epoll内核表删除fd上所注册的事件
static void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true){
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart){
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//进程池的构造函数，listenfd必须才创建进程池之前被创建，否则子进程无法直接饮用它
template<typename T>
processpool<T>::processpool(int listenfd, int process_number)
    :m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false){
        assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

        m_sub_process = new process[process_number];
        assert(m_sub_process);

        //创建process_number个子进程，并建立他们和父进程之间的管道
        for(int i = 0; i < process_number; ++i){
            int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
            assert(ret == 0);

            m_sub_process[i].m_pid = fork();
            assert(m_sub_process[i].m_pid >= 0);
            if(m_sub_process[i].m_pid > 0){//parent 关闭1端
                close(m_sub_process[i].m_pipefd[1]);
                continue;
            }
            else{ //child  关闭2端
                close(m_sub_process[i].m_pipefd[0]);
                m_idx = i;
                break;
            }
        }
    }

//统一事件源
template< typename T>
void processpool<T>::setup_sig_pipe(){
    //创建epoll事件监听表和信号管道
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd, sig_pipefd[0]);

    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

//父进程中m_idx值为-1， 子进程m_idx值大于等于0， 由此判断是run父还是run子进程
template<typename T>
void processpool<T>::run()
{
    if(m_idx != -1){
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child(){
    setup_sig_pipe();

    //每个子进程通过进程池的索引号m_idx找到与父进程通信的管道
    int pipefd = m_sub_process[m_idx].m_pipedfd[1];
    //子进程需要监听管道文件描述符pipefd，因为父进程来通知子进程accept的返回的新连接
    addfd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USR_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;
    
    while(!m_stop){
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        for(int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;
            if((sockfd == pipefd) && (events[i].events & EPOLLIN)){
                int client = 0;
                //从父子进程通信管道读取数据，并将结果保存到变量client中，
                //读取成功表明有新的客户连接
                ret = recv(sockfd, (char*)&client, sizeof(client), 0);
                if(((ret < 0) && (errno != EAGAIN)) || ret == 0){
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlen = sizeof(cilent_address);
                    int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                    
                    if(connfd < 0){
                        printf("errno is: %d\n", errno);
                        continue;
                    }
                    addfd(m_epollfd, connfd);
                    users[connfd].init(m_epollfd, connfd, client_address);
                }
            }
            else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)){
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if(ret <= 0){
                    continue;
                }else{
                    for(int i = 0; i < ret; ++i){
                        switch (signals[i])
                        {
                            case SIGCHLD:{
                                pid_t pid;
                                int stat;
                                while((pid == waitpid(-1, &stat, WNOHANG)) > 0){//回收所有僵尸进城
                                    continue
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:{
                                m_stop = true;
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN){
                users[sockfd].process();
            }
            else
            {
                continue;
            } 
        }
    }
    delete [] users;
    users = NULL;
    close(pipefd);
    //close(m_listenfd) 应该有m_listenfd的创建者来关闭这个文件描述符
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent(){
    setup_sig_pipe();

    //父进程监听m_listenfd
    addfd(m_epollfd, m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while(!m_stop){
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTER)){
            printf("epoll failure\n");
            break;
        }
        for(int i = 0; i < number, ++i){
            int sockfd = events[i].data.fd;
            if(sockfd == m_listenfd){
                //如果有新的连接到来，就采用round robin方式分配一个子进程处理
                int i = sub_process_counter;
                do{
                    if(m_sub_process[i].m_pid != -1){
                        break;
                    }
                    i = (i + 1)%m_process_number;
                }
                while(i != sub_process_counter);

                if(m_sub_process[i].m_pid == -1){
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                send(m_sub_process[i].m_pipedfd[0], (char*)&new_conn, sizeof(new_conn), 0);
                printf("send request to child %d \n", i);
            }
            else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)){
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if(ret <= 0){
                    continue;
                }
                else
                {
                    for(int i = 0; i < ret; ++i){
                        switch(signals[i]){
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ((pid == waitpid(-1, &stat, WNOHANG)) > 0)
                                {   
                                    for(int i = 0; i < m_process_number; ++i){
                                        //若i子进程退出，则主进程关闭乡音的通信管道，m_pid置为-1表示子进程退出
                                        if(m_sub_process[i].m_pid == pid){
                                            printf("child %d join\n", i);
                                            close(m_sub_process[i].m_pipedfd[0]);
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
                                for(int i = 0; i < m_process_number; ++i){
                                    if(m_sub_process[i].m_pid != -1){
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                //若父进程接收到终止信号，那么就杀死所有子进程，并等他们全部结束
                                printf("kill all the child now \n");
                                for(int i = 0; i < m_process_number; ++i){
                                    int pid = m_sub_process[i].m_pid;
                                    if(pid != -1){
                                        kill(pid, SIGTERM);
                                    }
                                }
                                break;
                            }
                            default:{
                                break;
                            }
                        }
                    }
                }
                
            }
            else{
                continue;
            }
            
        }
    }
    //close(m_listenfd); 由创建者关闭这个文件描述符
    close(m_epollfd);
}

#endif
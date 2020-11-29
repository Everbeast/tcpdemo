#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


//子进程的类，m_pid是目标子进程的PID， m_pipefd是父子进程的通信管道
class process{
public:
    process(): m_pid(-1){}
public:
    pid_t m_pid;
    int m_pipedfd[2];
}

//进程池类，模板参数是处理逻辑任务的类
template <typename T>
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
    //每个进程对应一个epoll内核时间表，用m_epollfd标识
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
            if(m_sub_process[i].m_pid > 0){//parent 关闭写端
                close(m_sub_process[i].m_pipefd[1]);
                continue;
            }
            else{ //child  关闭读端
                close(m_sub_process[i].m_pipefd[0]);
                m_idx = i;
                break;
            }
        }
    }

//统一事件源
template< typename T>
void processpoll<T>::setup_sig_pipe(){
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
                    users[conn]
                }
                
            }
        }
    }
}
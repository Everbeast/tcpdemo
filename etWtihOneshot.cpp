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
#include <pthread.h>

//et模式中，并发过程中，读取玩某个socket上的数据后开始处理数据，这个处理过程中
//socket又有新的数据可读（触发epollin），另一个线程被唤醒 
//这样就you两个线程同时操作一个socket的局面
//oneshot就是处理避免出现这种情况

//注意处理完后要取消oneshot，这样才能样其他工作线程有机会处理这个socket

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024
struct fds{
    int epollfd;
    int sockfd;
};

//将文件描述符设置为非阻塞
int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL); //获取文件配置
    int new_option = old_option | O_NONBLOCK; //设置为非阻塞
    fcntl(fd, F_SETFL, new_option); //设置文件配置
    return old_option;
}

//设置oneshot
void addfd(int epollfd, int fd, bool oneshot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if(oneshot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//重置oneshot,使得能且只能触发一次fd上的EPOLLIN事件
void reset_oneshot(int epollfd, int fd){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//woking threads
void* worker(void* arg){
    int sockfd = ((fds*)arg)->sockfd;
    int epollfd = ((fds*)arg)->epollfd;
    printf("start new thread to receive data on fd: %d\n", sockfd);
    char buf[BUFFER_SIZE];
    memset(buf, '\0', BUFFER_SIZE);
    //循环读取sockfd上的数据，知道遇到EAGAIN错误
    while (1)
    {
        int ret = recv(sockfd, buf, BUFFER_SIZE-1, 0);
        if(ret == 0){
            close(sockfd);
            printf("foreigner closed the connection\n");
            break;
        }
        else if(ret < 0){
            if(errno == EAGAIN){
                reset_oneshot(epollfd, sockfd);
                printf("read later\n");
                break;
            }
        }else{
            printf("get content: %s\n", buf);
            sleep(5);
        }
    }
    printf("end thread receiving data on fd: %d\n", sockfd);
    
}

int main(int argc, char* argv[]){
    if(argc <= 2){
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    //监听listenfd不能注册为oneshot，否则只能处理一个客户连接
    addfd(epollfd, listenfd, false);

    while (1)
    {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(ret < 0){
            printf("epoll failurer\n");
            break;
        }
        for(int i = 0; i < ret; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addlen);
                addfd(epollfd, connfd, true);
            }
            else if(events[i].events & EPOLLIN){
                pthread_t thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;
                //启动一个工作线程为sockfd服务
                pthread_create(&thread, NULL, worker, (void*)&fds_for_new_worker);
            }
            else{
                printf("something else happened \n");
            }
        }
    }
    close(listenfd);
    return 0;
    
}

#include <stdlib.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <poll.h>
#include <sys/epoll.h>
#include <iostream>
#include <algorithm>

void sig_child(int signo){
    pid_t pid;
    int stat;
    while((pid = waitpid(-1, &stat, WNOHANG)) > 0){
        printf("child %d terminated \n", pid);
    }
    return;
}


void str_echo(int fd){
    ssize_t n;
    char buf[1024];
    again:
        while ((n = read(fd, buf, 1024)) > 0){
            write(fd, buf, 1024);
            if(n < 0 && errno == EINTR)
                goto again;
            else if (n < 0)
                perror("str_echo: read error\n");
        }
}

//第五章 fork客户端
// int main(){

//     int listenfd, connfd;
//     pid_t chidpid;
//     socklen_t clilen;
//     struct sockaddr_in cliaddr, servaddr;
    
//     listenfd = socket(AF_INET, SOCK_STREAM, 0);

//     bzero(&servaddr, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
//     servaddr.sin_port = htons(8090);

//     bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
//     listen(listenfd, 128); //listen queue 128
//     signal(SIGCHLD, sig_child);

//     // clilen = sizeof(cliaddr);
//     while(1){
//         clilen = sizeof(cliaddr);
//         //和客户端的connect完成了三次握手 accept得到返回，建立完成
//         if((connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen)) < 0){
//             if(errno == EINTR){
//                 continue;
//             }else{
//                 perror("accept errorr");
//             }
//         }
//         //服务端开始fork子进程
//         //child调用str_echo 阻塞在read，等待客户端送入一行文本
//         //parent再次调用accept并阻塞，等待下一个客户连接

//         if ((chidpid = fork()) == 0){
//             close(listenfd);
//             str_echo(connfd);
//             exit(0);
//         }else if(chidpid > 0){
//             close(connfd);
//         }
        
//     }
// }
//Proto Recv-Q Send-Q  Local Address           Foreign Address         State      
//tcp        0      0  0.0.0.0:9877            0.0.0.0:*               LISTEN

//第六张 select I/O复用服务器

// int main(){
//     int listenfd = socket(AF_INET, SOCK_STREAM, 0);
//     //套接字构造体
//     struct sockaddr_in servaddr;
//     bzero(&servaddr, sizeof(servaddr));
//     servaddr.sin_family = AF_INET;
//     servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
//     servaddr.sin_port = htons(8090);

//     bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

//     listen(listenfd, 128);

    
//     int maxi = -1;
//     int client[1023];
//     for(int i = 0; i < 1023; i++){
//         client[i] = -1;
//     }
//     fd_set oldset, newset;
//     FD_ZERO(&oldset);
//     FD_SET(listenfd, &oldset);
//     int maxfd = listenfd;

//     struct sockaddr_in cliaddr;
//     socklen_t clilen = sizeof(cliaddr);
//     int i;
//     while(1){
//         int connfd;
//         //两个set分离， 因为用了要清0
//         newset = oldset;
//         int nready = select(maxfd + 1, &newset, NULL, NULL, NULL);

//         //判断select就绪的set
//         if(FD_ISSET(listenfd, &newset)) // 服务器就绪
//         {
//             connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
//             //加入监听set
//             FD_SET(connfd, &oldset);

//             //找到可用的位置，存入客户端的描述符
//             for(i = 0; i < 1023; i++){
//                 if(client[i] == -1){
//                     client[i] = connfd;
//                     break;
//                 }
//             }
//             if(i == 1023)
//                 perror("too many clients");
            
//             //记录用到的连接数目
//             if(maxi < i){
//                 maxi = i;
//             }
//             //更新最大描述符
//             if(connfd > maxfd){
//                 maxfd = connfd;
//             }

//             if(--nready <= 0){
//                 continue; //no more readable descriptors
//             }
//         }
//         //客户端就绪
//         for(i = 0; i <= maxi; i++){
//             if(client[i] < 0)
//                 continue; //-1表示没有用到的或者关闭了

//             //准备就绪的客户端文件描述符
//             if(FD_ISSET(client[i], &newset)){
//                 int n;
//                 char buf[1500];
//                 if((n = read(client[i], buf, sizeof(buf))) > 0){
//                     //echo back to client
//                     write(client[i], buf, n);
//                 }else if(n==0){
//                     close(client[i]);
//                     FD_CLR(client[i], &oldset);
//                     client[i] = -1;
//                 }
//                 if(--nready <= 0){
//                     break;
//                 }
//             }
//         }
//         //FD有改变的都是oldset，下一个循环用
//     }
// }

//第六张 poll I/O复用服务器
int main(){
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    //套接字构造体
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(8090);

    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    listen(listenfd, 128);

    
    struct pollfd client[4096]; //open num is 4096
    client[0].fd = listenfd;
    client[0].events = EPOLLIN; //POLLIN表示 普通或优先数据可读
    //其他设为-1 表示还没用到
    for(int i = 1; i < 4096; i++){
        client[i].fd = -1;
    }
    int maxi = 0; //表最大的客户端描述符的数目
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    int i;
    while (1)
    {
        int connfd;
        int nready = poll(client, maxi + 1, -1); //-1表示无限时间？

        if(client[0].revents & POLLIN){ //服务端监听就绪
            connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);

            for(i = 1; i < 4096; i++){
                if(client[i].fd == -1){
                    client[i].fd = connfd;
                    break;
                }
            }
            if(i == 4096){
                perror("too many clients");
            }

            client[i].events = POLLIN;
            if(i > maxi){
                maxi = i;
            }
            if(--nready <= 0)
                continue;
        }

        //客户端准备就绪
        for(int i = 1; i <= maxi; i++){
            if(client[i].fd == -1)
                continue;
            
            int n;
            char buf[1500];
            //客户端有读时间
            if(client[i].revents & POLLIN){
                if((n = read(client[i].fd, buf, 1500)) > 0){
                    //回写到client
                    write(client[i].fd, buf, n);
                }else if(n == 0){
                    close(client[i].fd);
                    client[i].fd = -1;
                }

                if(--nready <=0){
                    break;
                }
            }
        }
    }
    
}
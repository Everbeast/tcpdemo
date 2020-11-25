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

void str_cli(FILE *fp, int sockfd){
    // char sendline[1500], recvline[1500];
    // //fgets 读入一行文本
    // while(fgets(sendline, 1500, fp) != NULL){
    //     //write把该行发送给服务器
    //     write(sockfd, sendline, strlen(sendline));
    //     //从服务器读入回射行
    //     // if(read(sockfd, recvline, 1500) == 0)
    //     //     perror("str_cli: server terminated prematurely");
    //     read(sockfd, recvline, 1500);
    //     //fputs把它写到标准输出
    //     std::cout<< recvline << std::endl;
    //     fputs(recvline, stdout);
    // }
    char buf[1500];
    int len;
    while(fgets(buf, sizeof(buf), fp)!= NULL){
        write(sockfd, buf, strlen(buf));
        std::cout<<"客户端发送内容:"<<buf;
        bzero(buf, sizeof(buf));
        read(sockfd, buf, sizeof(buf));
        std::cout<<"服务器返回内容:"<<buf;
        bzero(buf, sizeof(buf));
        
    }
}

void str_cli_select(FILE *fp, int sockfd){
    int maxfd, stdineof = 0;

    int n;
    fd_set oldset;
    char buf[1024];
    bzero(buf, 1024);

    FD_ZERO(&oldset);
    while(1){
        //将 读 加入到select 即输入 stdin
        if(stdineof == 0)
            FD_SET(fileno(fp), &oldset);
        //将发送加入到select 即套接字
        FD_SET(sockfd, &oldset);

        //获取select的最大描述符
        maxfd = max(fileno(fp), sockfd) + 1;
        select(maxfd, &oldset, NULL, NULL, NULL);

        if(FD_ISSET(fileno(fp), &oldset)){
            if((n = read(sockfd, buf, 1024)) == 0){
                //若标注你输入碰上eof时 将标志stdineof = 1
                stdineof = 1;
                //然后shotdown
                shutdown(sockfd, SHUT_WR);
                FD_CLR(fileno(fp), &oldset);
                continue;
            }
            write(sockfd, buf, strlen(buf));
            std::cout<<"客户端发送内容: "<<buf;
            bzero(buf, sizeof(buf));
        }

        if(FD_ISSET(sockfd, &oldset)){
            if((n = read(sockfd, buf, 1024)) == 0){
                if(stdineof == 1){ //遇到EOF正常终止
                    return;
                }else{
                    perror("error");
                }
            }
            std::cout<<"服务器返回的内容： "<<std::endl;
            // write(fileno(stdout), buf, n);//c的写法？
            bzero(buf, sizeof(buf));
        }

    }
}

int main(int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    if(argc != 2)
        perror("usage: echoClient <IPaddress>");
    
    //创建套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8090);
    //讲参数的ip地址转换成二进制 给到server结构体
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    //连接，引起tcp三路握手
    connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    //建立完成，调用str_cli函数 fgets阻塞，等待输入
    // str_cli(stdin, sockfd);
    str_cli_select(stdin, sockfd);
    exit(0);
}

/*
Proto Recv-Q Send-Q Local Address           Foreign Address         State      
tcp        0      0 0.0.0.0:9877            0.0.0.0:*               LISTEN 
tcp        0      0 localhost:58164         localhost:9877          ESTABLISHED  客户端进程套接字
tcp        0      0 localhost:9877          localhost:58164         ESTABLISHED  服务器子进程套接字

$ ps a -o pid,ppid,tty,stat,args,wchan | grep -E 'PID|echo'
    PID    PPID TT       STAT COMMAND                     WCHAN
  14247   14098 pts/0    S+   ./echoServer                inet_csk_accept //等待连接 阻塞在accept
  15403   15358 pts/3    S+   ./echoClient 127.0.0.1      wait_woken    //阻塞在 fgets
  15404   14247 pts/0    S+   ./echoServer                wait_woken   //子进程 阻塞在read

*/
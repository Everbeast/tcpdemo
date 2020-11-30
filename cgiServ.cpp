#include "processpool.h"

//用于处理客户cgi请求的类，可以作为processpool类的模板参数
class cgi_conn
{
public:
    cgi_conn(){}
    ~cgi_conn(){}
    //初始化连接，清空缓冲区
    void init(int epollfd, int sockfd, const sockaddr_in& client_addr){
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buf, '\0', BUFFER_SIZE);
        m_read_idx = 0;
    }

    void process(){
        int idx = 0;
        int ret = -1;
        //循环读取和分析客户数据
        while(true){
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE-1-idx, 0);
            //如果读取操作发生错误，则关闭客户连接
            if(ret < 0){
                if(errno != EAGAIN){
                    removefd(m_epollfd, m_sockfd);
                }
                break;
            }
            else if(ret == 0){
                //若果对方关闭连接， 服务器也关闭连接
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else{
                m_read_idx += ret;
                printf("user content is: %s\n", m_buf);
                //如果遇到字符“\r\n”，开始处理客户请求
                for(; idx < m_read_idx; ++idx){
                    if((idx >= 1) && (m_buf[idx-1] == '\r') && (m_buf[idx] == '\n')){
                        break;
                    }
                }
                if(idx == m_read_idx){
                    continue;
                }
                m_buf[idx - 1] = '\0';

                char* file_name = m_buf;
                //判断客户要运行的cgi程序是否存在， 不存在则关闭连接
                if(access(file_name, F_OK) == -1){
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                //创建子进程来执行cgi程序
                ret = fork();
                if(ret == -1){
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else if(ret > 0){
                    //父进程只需要关闭连接
                    removefd(m_epollfd, m_sockfd);
                    break;
                }
                else
                {
                    {
                        //子进程将标准输出定向到m_sockfd，并执行cgi程序
                        close(STDOUT_FILENO);
                        dup(m_sockfd);
                        execl(m_buf, m_buf, 0);
                        exit(0);
                    }
                }
                
            }
        }
    }

private:
    //读缓冲区的大小
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];
    int m_read_idx;
};
int cgi_conn::m_epollfd = -1;

int main(int argc, char* argv[])
{
    if(argc <= 2){
        printf("usage: %s ip_address port_number \n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);

    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
    if(pool){
        pool->run();
        delete pool;
    }

    close(listenfd); //由创建者关闭描述符listenfd

    return 0;
}
#ifndef LST_TIMER
#define LST_TIMER

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define BUFFER_SIZE 64
class util_timer; //声明


//用户数据结构
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer* timer;
};

//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL){}

public:
    time_t expire; //任务超时时间
    void (*cb_func)(client_data*); //回调函数
    //回调函数处理的客户数据由定时器执行者传给灰调函数
    client_data* user_data;
    util_timer* prev;
    util_timer* next;
};

//定时器链表：升序、双向，带有头尾节点
class sort_time_lst
{
public:
    sort_time_lst() : head(NULL), tail(NULL){}
    //析构链表要删除所有定时器
    ~sort_time_lst(){
        util_timer* tmp = head;
        while (tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        } 
    }

    //将目标定时器timer添加到链表中
    void add_timer(util_timer* timer){
        if(!timer){
            return;
        }
        if(!head){ //若头为空
            head = tail = timer;
            return;
        }
        if(timer->expire < head->expire){//若插入的超时时间小于头结点的，则头插入新timer
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        //其他情况调用重载函数
        add_timer(timer, head);
    }

    void adjust_timer(util_timer* timer)
    {
        if(!timer){
            return;
        }
        util_timer* tmp = timer->next;

        //若timer是最后节点，或者 timer小于后面的时间，不需要调整
        if(!tmp || (timer->expire < tmp->expire)){
            return;
        }
        //拆开节点，重新调用add_timer加入源节点后面开始的节点（因为expire比timer->next要大）
        if(timer == head){
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }else{
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void del_timer(util_timer* timer)
    {
        if(!timer){
            return;
        }
        if((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }

        if(timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        
        if(timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }

        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
        return;
    }

    //sigalrm信号每次被出发就在信号处理函数执行一次tick函数，用来处理链表上到期的任务
    void tick()
    {
        if(!head){
            return;
        }
        printf("timer tick\n");
        time_t cur = time(NULL); //获取当前时间
        util_timer* tmp = head;

        while (tmp)
        {
            //现在时间没有超时，则退出循环->退出tick函数
            if(cur < tmp->expire){
                break;
            }

            //调用定时器的回调函数执行定时任务
            //数据又调用回调函数时传入
            tmp->cb_func(tmp->user_data);
            //执行完之后，跳到下一个timer
            head = tmp->next;
            if(head){
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }
private:
    //重载函数 被adjust timer 和add timer调用
    void add_timer(util_timer* timer, util_timer* lst_head)
    {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        while(tmp){
            if(timer->expire < tmp->expire){
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                // break;
                return;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        //timer比后面的都大
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }

private:
    util_timer* head;
    util_timer* tail;
};


#endif
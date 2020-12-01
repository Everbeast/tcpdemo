#ifndef WHEEL_TIMER
#define WHEEL_TIMER

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

#define BUFFER_SIZE 64
class tw_timer;

//绑定socket和定时器
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer* timer;
};

//定时器类
class tw_timer
{
public:
    tw_timer(int rot, int ts):
        next(NULL), prev(NULL),rotation(rot), time_slot(ts){}

public:
    int rotation; //记录要转多少圈
    int time_slot; //定时器属于时间轮的哪个槽上
    void (*cb_func)(client_data*);
    client_data* user_data;
    tw_timer* next;
    tw_timer* prev;
};

class time_wheel
{
public:
    time_wheel():cur_slot(0){
        for(int i = 0; i < N; ++i){
            slots[i] = NULL;
        }
    }
    ~time_wheel(){
        //遍历每个槽 销毁定时器
        for(int i = 0; i < N; ++i){
            tw_timer* tmp = slots[i];
            while (tmp)
            {
                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }

    //根据定时值 插入合适的槽中
    tw_timer* add_timer(int timeout)
    {
        if(timeout < 0){
            return NULL;
        }
        //计算经过的槽数
        int ticks = 0;
        if(timeout < SI){
            ticks = 1;
        }else
        {
            ticks = timeout / SI;
        }

        //计算旋转的圈数
        int rotation = ticks / N;
        //计算合适的槽
        int ts = (cur_slot + (ticks % N)) % N;
        tw_timer* timer = new tw_timer(rotation, ts);

        //插入槽中
        if(!slots[ts]){
            printf("add timer, rotation is %d, ts is %d, cur_slot is %d\n",
                rotation, ts, cur_slot);
            slots[ts] = timer;
        }else{
            timer->next = slots[ts];
            slots[ts]->prev = timer;
            slots[ts] = timer;
        }
        return timer;
    }

    void del_timer(tw_timer* timer){
        if(!timer){
            return;
        }
        int ts = timer->time_slot;
        if(timer == slots[ts]){
            slots[ts] = slots[ts]->next;
            if(slots[ts]){
                slots[ts]->prev = NULL;
            }
            delete timer;
        }
        else
        {
            timer->prev->next = timer->next;
            if(timer->next)
                timer->next->prev = timer->prev;
            delete timer;
        }
    }

    void tick()
    {
        tw_timer* tmp = slots[cur_slot]; 
        printf("current slot is %d \n", cur_slot);
        while (tmp)
        {
            printf("tick the timer once \n");
            if(tmp->rotation > 0){ //若rotation>=1，表示要到下一轮以上才触发
                tmp->rotation--;
                tmp = tmp->next;
            }
            else //死期已到
            {
                tmp->cb_func(tmp->user_data);
                //执行完任务则删除该定时器
                if(tmp == slots[cur_slot])
                {
                    printf("delete header in cur_slot\n");
                    slots[cur_slot] = tmp->next;
                    delete tmp;
                    if(slots[cur_slot]){
                        slots[cur_slot]->prev = NULL;
                    }
                    tmp = slots[cur_slot];
                }
                else
                {
                    tmp->prev->next = tmp->next;
                    if(tmp->next){
                        tmp->next->prev = tmp->prev;
                    }
                    tw_timer* tmp2 = tmp->next;
                    delete tmp;
                    tmp = tmp2;
                }
                
            }
            
        }
        cur_slot = ++cur_slot % N; //更新槽游标
        
    }

private:
    //时间轮上槽的数据
    static const int N = 60;
    //每秒钟转动一次，即槽间间隔1s
    static const int SI = 1;
    //时间轮的槽，指向一个定时器链表，标了无序
    tw_timer* slots[N];
    int cur_slot; //指向槽的游标
};

#endif
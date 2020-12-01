#ifndef MIN_HEAP
#define MIN_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>
using std::exception;

#define BUFFER_SIZE 64

class heap_timer; //前向声明

struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer* timer;
};

//定时器类
class heap_timer
{
public:
    heap_timer(int delay)
    {
        expire = time(NULL) + delay;
    }
public:
    time_t expire; //定时器生效的绝对时间
    void (*cb_func)(client_data*);
    client_data* user_data;
};

//时间堆类
class time_heap
{
public:

    //构造1
    time_heap(int cap) : capacity(cap), cur_size(0)
    {
        array = new heap_timer*[capacity]; //创建堆数据，元素是heap_timer的指针
        if(!array){
            throw std::exception();
        }
        for(int i = 0; i < capacity; ++i)
        {
            array[i] = NULL;
        }
    }
    //构造2
    time_heap(heap_timer** init_array, int size, int capacity)
        : cur_size(size), capacity(capacity)
    {
        if(capacity < size){
            throw std::exception();
        }
        array = new heap_timer* [capacity];
        if(!array){
            throw std::exception();
        }
        for(int i = 0; i < capacity; ++i){
            array[i] = NULL;
        }
        if(size != 0)
        {
            for(int i = 0; i < size; ++i)
            {
                array[i] = init_array[i];
            }
            for(int i = (cur_size - 1)/2; i >= 0; --i)
            {
                //堆数组的前一半数据进行下虑
                percolate_down(i);
            }
        }
    }
    //析构
    ~time_heap()
    {
        for(int i = 0; i < cur_size; ++i)
        {
            delete array[i];
        }
        delete [] array;
    }

public:
    void add_timer(heap_timer* timer)
    {
        if(!timer)
            return;
        
        if(cur_size >= capacity)
            resize(); //扩大数组一倍

        //插入新元素，堆大小+1， hole是新建的位置
        int hole = cur_size++;
        int parent = 0;
        //对空穴进行上虑操作
        for(;hole > 0; hole = parent)
        {
            parent = (hole-1) / 2;
            if(array[parent]->expire <= timer->expire){
                break; //符合最小堆 ，不用变换
            }
            //hole小于其parent 则调换位置，继续循环
            array[hole] = array[parent]; 
        }
        array[hole] = timer; //找到最终位置
    }

    void del_timer(heap_timer* timer)
    {
        if(!timer)
            return;
        
        //延迟撤销(只把回调函数置空），节省时间，但容器使堆膨胀
        timer->cb_func = NULL;
    }

    heap_timer* top() const
    {
        if(empty())
            return NULL;
        return array[0];
    }

    //删除堆顶定时器
    void pop_timer()
    {
        if(empty())
            return;
        if(array[0]){
            delete array[0];
            //将最后一个元素放到堆顶 然后进行下虑
            array[0] = array[--cur_size];
            percolate_down(0);
        }
    }

    void tick()
    {
        heap_timer* tmp = array[0];
        time_t cur = time(NULL);
        while (!empty())
        {
            if(!tmp){
                break;
            }
            if(tmp->expire > cur){
                break; //没到死期
            }
            //否则执行堆顶的timer的任务，并删除
            if(array[0]->cb_func){
                array[0]->cb_func(array[0]->user_data);
            }
            pop_timer();
            tmp = array[0];
        }
        
    }

    bool empty() const{
        return cur_size == 0;
    }

private:

    void percolate_down(int hole){
        heap_timer* temp = array[hole]; //保存该位置的数据
        int child = 0;
        for(; (hole*2+1) <= (cur_size - 1); hole = child){
            child = hole*2 + 1;
            //现在child是左子，child+1是右子， 选小的那个
            if((child < (cur_size - 1)) && (array[child + 1]->expire < array[child]->expire)){
                ++child;
            }

            if(array[child]->expire < array[hole]->expire){
                array[hole] = array[child];
            }
            else
            {
                break;
            }
        }
        array[hole] = temp;
    }
    //double size
    void resize(){
        heap_timer** temp = new heap_timer* [2*capacity];
        for(int i = 0; i < 2*capacity; ++i){
            temp[i] = NULL;
        }
        if(!temp){
            throw std::exception();
        }
        for(int i = 0; i < cur_size; ++i){
            temp[i] = array[i];
        }
        delete [] array;
        array = temp;
    }


private:
    heap_timer** array; //堆数组
    int capacity; //堆容量
    int cur_size; //现在包含timer的个数
};

#endif
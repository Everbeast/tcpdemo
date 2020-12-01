
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

//半同步、半反应堆线程池实现
/*
半同步半异步的变体
其中，异步线程只有一个，即主线程，负责监听所有socket事件，
如果socket上有可读事件，表有新的连接请求，主线程接受该新连接，
往epoll事件表注册这个事件。若该socket有读事件发生，表有数据要发送到客户端，
主线程就将这个socket插入到请求队列中（插入的是就绪链接的socket）。所有worker线程都睡眠在请求队列上，
有任务到来时，通过竞争（申请互斥锁）来获得任务接管权。

缺点：
主线程和工作线程共享请求队列，主线程往请求队列添加任务或者工作线程取出请求队列
都要对队列加锁，耗费cpu
每个工作线程同一时间只能处理一个客户请求，若客户多，工作线程少，请求队列就会
堆积很多，导致客户响应速度将越来越慢（排队长） 若是增加工作线程，则线程切换耗费cpu
*/

template<typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //往请求队列中添加任务
    bool append(T* request);
private:
    //工作线程运行的函数 它不断从工作队列中取出任务并执行
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number; //线程池的线程数
    int m_max_requests; //请求队列中允许的最大请求数
    phtread_t* m_threads; //描述线程池的数组，大小为m_thread_number
    std::list<T*>m_workqueue; //请求队列
    locker m_queuelocker; //保护请求队列的互斥锁
    sem m_queuestat; //是否有任务需要处理
    bool m_stop; //是否结束线程
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number),m_max_requests(max_requests),
    m_stop(false),m_threads(NULL)
{
    if((thread_number <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }

    m_threads = new thread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }

    //创建thread_number 个线程 并将它们都设置为脱离线程
    for(int i = 0; i < thread_number; ++i)
    {
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* requests)
{
    //操作工作队列时一定要加锁， 因为它被所有线程共享
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock;
        return false;
    }
    m_workqueue.push_back(requests);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        request->process();
    }
}
#endif
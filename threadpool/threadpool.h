#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <thread>       // [新增] 引入 C++ 线程库
#include <vector>       // [新增] 引入 vector
// #include <pthread.h> // [删除] 不再需要
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

/*
移除 Boilerplate (样板代码)：“旧代码必须定义一个 static void* worker(void* arg) 函数，然后把 this 指针强转回来才能调用类成员函数，这既繁琐又不安全。 
我使用了 C++11 的 Lambda 表达式，直接在构造函数中捕获 this 指针并启动线程，代码逻辑一气呵成，可读性大大提高。”
资源自动管理：“使用 std::vector<std::thread> 替代裸指针数组 pthread_t*，不需要在析构函数里手动 delete[]，遵循了 C++ 的 RAII 原则。”
*/

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    
    // 这两个函数保持不变
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /* [删除] 以前需要这个静态函数来适配 pthread_create，现在不需要了 */
    // static void *worker(void *arg);
    
    // 实际的工作函数
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    
    // [修改] 使用 vector 管理线程对象，替代原本的 pthread_t 数组
    // pthread_t *m_threads; 
    std::vector<std::thread> m_threads; 

    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};

// 构造函数实现
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) 
    : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    // [删除] 不需要手动 new 数组了
    // m_threads = new pthread_t[m_thread_number];
    
    for (int i = 0; i < thread_number; ++i)
    {
        // [核心修改] 使用 Lambda 表达式直接创建并启动线程
        // capture [this] 使得我们可以直接在线程里调用成员函数 run()
        m_threads.emplace_back([this]() {
            this->run();
        });

        // 线程分离：让线程在后台运行，不需要主线程 join 等待
        m_threads.back().detach();
        
        /* 旧代码对比：
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads; throw std::exception();
        }
        if (pthread_detach(m_threads[i])) { ... }
        */
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    // [删除] vector 会自动析构，不需要手动 delete
    // delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    // 这里使用我们在 locker.h 里定义的 lock()，接口完全兼容
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 使用新版 sem 的 post
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/* [删除] worker 函数实现已经删除了 */
// template <typename T>
// void *threadpool<T>::worker(void *arg) { ... }

// 替换 threadpool.h 中的 run() 函数
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        // Reactor 模式 (m_actor_model == 1)
        if (1 == m_actor_model)
        {
            if (request->m_state == 0)
            {
                // [修改 1] 使用赋值接收返回值
                request->mysql = connection_pool::GetInstance()->GetConnection();
                
                // 执行业务逻辑
                request->process(); 
                
                // 归还连接
                connection_pool::GetInstance()->ReleaseConnection(request->mysql);
            }
        }
        // Proactor 模式 (m_actor_model == 0)
        else
        {
            // 直接处理，不需要再次 read_once 或 write
            request->mysql = connection_pool::GetInstance()->GetConnection();
            
            // process() 会解析缓冲区数据，并注册 EPOLLOUT 事件
            // 让主线程在后续的事件循环中负责发送数据
            request->process(); 
            
            connection_pool::GetInstance()->ReleaseConnection(request->mysql);
            
            // Proactor模式下，webserver.cpp 主线程并没有等待 improv 标志
            // 所以这里只需要完成 process() 即可
        }
    }
}
#endif
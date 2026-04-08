#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cstdio>
#include <exception>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

// 假设 connection_pool 已经定义，具有 GetConnection() 和 ReleaseConnection(conn) 方法
// 注意：这里不再使用全局单例，而是通过构造函数传入连接池指针
class connection_pool;

template <typename T>
class threadpool {
public:
    /**
     * @param actor_model 1: Reactor模式, 0: Proactor模式
     * @param connPool    数据库连接池指针（可为nullptr，表示不需要数据库）
     * @param thread_num  工作线程数量
     * @param max_requests 请求队列最大长度
     */
    threadpool(int actor_model, connection_pool* connPool,
               int thread_num = 8, int max_requests = 10000);
    ~threadpool();

    // 禁止拷贝和移动（线程池不应被复制）
    threadpool(const threadpool&) = delete;
    threadpool& operator=(const threadpool&) = delete;

    // 添加请求（带状态，用于 Reactor 模式）
    bool append(T* request, int state);
    // 添加请求（不带状态，用于 Proactor 模式）
    bool append_p(T* request);

    // 停止线程池，等待所有线程结束
    void stop();

private:
    void run();  // 工作线程主函数

private:
    const int m_actor_model;
    const int m_thread_number;
    const int m_max_requests;
    connection_pool* m_connPool;          // 现在会被实际使用

    std::vector<std::thread> m_threads;   // 线程对象，析构时会自动 join（需要 stop 标志配合）
    std::queue<T*> m_workqueue;           // 请求队列（使用 queue 更贴合 FIFO）
    mutable std::mutex m_queue_mutex;     // 保护队列的互斥锁
    std::condition_variable m_queue_cv;   // 条件变量，替代信号量
    std::atomic<bool> m_stop;             // 停止标志
};

// ----------------------------- 实现 -----------------------------

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool,
                          int thread_num, int max_requests)
    : m_actor_model(actor_model),
      m_thread_number(thread_num),
      m_max_requests(max_requests),
      m_connPool(connPool),
      m_stop(false) {
    if (thread_num <= 0 || max_requests <= 0) {
        throw std::invalid_argument("thread_num and max_requests must be positive");
    }

    try {
        for (int i = 0; i < m_thread_number; ++i) {
            m_threads.emplace_back([this] { run(); });
        }
    } catch (...) {
        // 如果创建任何线程失败，先停止标志并等待已创建的线程结束
        m_stop = true;
        m_queue_cv.notify_all();          // 唤醒所有线程，让它们检查停止标志并退出
        for (auto& t : m_threads) {
            if (t.joinable()) t.join();
        }
        throw;  // 重新抛出原始异常
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    stop();  // 确保所有线程安全结束
}

template <typename T>
void threadpool<T>::stop() {
    m_stop = true;
    m_queue_cv.notify_all();   // 唤醒所有等待的线程
    for (auto& t : m_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

template <typename T>
bool threadpool<T>::append(T* request, int state) {
    if (m_stop) return false;  // 已停止，拒绝新任务

    std::unique_lock<std::mutex> lock(m_queue_mutex);
    if (m_workqueue.size() >= m_max_requests) {
        return false;  // 队列已满
    }
    request->m_state = state;
    m_workqueue.push(request);
    lock.unlock();
    m_queue_cv.notify_one();   // 唤醒一个工作线程
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T* request) {
    if (m_stop) return false;

    std::unique_lock<std::mutex> lock(m_queue_mutex);
    if (m_workqueue.size() >= m_max_requests) {
        return false;
    }
    m_workqueue.push(request);
    lock.unlock();
    m_queue_cv.notify_one();
    return true;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        T* request = nullptr;

        // 等待任务或停止信号
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait(lock, [this] {
                return m_stop || !m_workqueue.empty();
            });

            if (m_stop && m_workqueue.empty()) {
                // 线程池停止且无剩余任务，直接退出
                return;
            }

            request = m_workqueue.front();
            m_workqueue.pop();
        }  // 解锁

        if (!request) continue;

        // 处理请求（需要数据库连接时使用 m_connPool）
        if (m_actor_model == 1) {  // Reactor 模式
            if (request->m_state == 0) {  // 读阶段
                if (m_connPool) {
                    request->mysql = m_connPool->GetConnection();
                }
                request->process();
                if (m_connPool && request->mysql) {
                    m_connPool->ReleaseConnection(request->mysql);
                    request->mysql = nullptr;
                }
            }
            // 写阶段（m_state == 1）这里不处理，由主线程处理
        } else {  // Proactor 模式
            if (m_connPool) {
                request->mysql = m_connPool->GetConnection();
            }
            request->process();
            if (m_connPool && request->mysql) {
                m_connPool->ReleaseConnection(request->mysql);
                request->mysql = nullptr;
            }
        }
    }
}

#endif // THREADPOOL_H

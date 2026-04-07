#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <mutex>
#include <condition_variable>
#include <chrono>

/*
修改部分：
1、修改sem类
原版的 sem 类只是对 POSIX C API 的简单封装，移植性差且资源管理不安全（允许拷贝）。这个API的调用sem_wait直接调用系统API，对于用户来说是黑盒
我利用 C++11 的 std::mutex 和 std::condition_variable 手写实现了一个标准的计数信号量。在实现 wait 操作时，我利用了条件变量的 predicate 机制，显式构建了 while 循环来检查资源计数，从而从代码层面彻底解决了多线程虚假唤醒的问题。
同时，我禁用了拷贝构造函数，遵循 RAII 原则，提高了代码的鲁棒性。”

2、重写互斥锁（locker）：
在 C 语言风格编程中，资源（如锁、文件句柄、内存）的生命周期必须由程序员手动管理。pthread_mutex_init 负责“生”。pthread_mutex_destroy 负责“死”。
风险：如果你在类的维护过程中不小心删掉了析构函数里的 destroy，或者在构造函数里 init 之前就抛出了异常，都会导致资源泄漏或未定义行为。
新代码优势： std::mutex 是一个标准的 C++ 类，它遵循 RAII (资源获取即初始化) 原则。
当你声明 std::mutex m_mutex; 时，它的构造函数会自动被调用，完成系统底层的锁初始化。当 locker 对象销毁时，m_mutex 的析构函数会自动被调用，释放系统底层的锁资源。

3、重写条件变量：
条件变量的 adopt_lock 技巧：技术细节：旧代码的 pthread_cond_wait 要求传入一个已上锁的 mutex，并在返回时保持上锁状态。现代 C++ 的 cv.wait 需要 unique_lock。
解决方案：我使用了 std::unique_lock<std::mutex> ulock(*m_mutex, std::adopt_lock)。adopt_lock 告诉编译器：“这个锁已经被锁住了，你直接接管它，不要再 lock 了”。
关键点：在函数结束前调用 ulock.release()。如果不调用，ulock 析构时会自动 unlock，导致函数返回后外部的锁被意外解开。release() 可以切断 unique_lock 与 mutex 的联系，完美模拟了 pthread 的行为。
*/


// 封装信号量
class sem
{
public:
    // 构造函数，默认资源数为0
    sem(int num = 0) : m_count(num) {}

    // 禁用复制构造和赋值操作（RAII 资源的常见做法）
    sem(const sem&) = delete;
    sem& operator=(const sem&) = delete;
    
    ~sem() = default;

    // 等待信号量 (P操作)
    bool wait()
    {
        // 要想访问m_count，就必须要先对保护m_count的互斥锁m_mutex进行上锁
        std::unique_lock<std::mutex> lock(m_mutex);
        // cv是c++的标准条件变量，wait操作的意思是，循环等待m_count > 0，一旦满足条件就跳过继续，否则就解锁lock阻塞等待（方便别的线程归还资源）
        m_cv.wait(lock, [this]() { return m_count > 0; });
        --m_count;
        return true;
    }

    // 释放信号量 (V操作)
    bool post()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_count;
        // 这个函数的作用是，通知一个因为wait而阻塞等待的线程
        m_cv.notify_one();
        return true;
    }

    void init(int num){
        //lock_guard开销很小，作用于结束就解锁，不支持手动解锁，适合初始化的时候用
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count = num;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_count;
};


// 封装互斥锁 实现RAII重构并兼容原项目在这个上的接口
class locker
{
public:
    locker() = default;
    ~locker() = default;

    bool lock()
    {
        m_mutex.lock();
        return true;
    }

    bool unlock()
    {
        m_mutex.unlock();
        return true;
    }

    // 返回原生 mutex 指针，供 cond 使用
    std::mutex *get()
    {
        return &m_mutex;
    }

private:
    std::mutex m_mutex;
};



// 封装条件变量
class cond
{
public:
    cond() = default;
    ~cond() = default;

    // 核心修改：为了兼容旧代码的调用方式 (传入 mutex 指针)
    // 我们在内部构造 unique_lock 并“领养”(adopt) 这个锁
    bool wait(std::mutex *m_mutex)
    {
        // std::adopt_lock 表示 m_mutex 已经被调用者锁住了，这里接管所有权
        std::unique_lock<std::mutex> ulock(*m_mutex, std::adopt_lock);
        
        m_cv.wait(ulock);
        
        // 重要：wait 返回后，ulock 拥有锁。
        // 但为了配合旧代码逻辑（函数返回时锁依然由调用者持有），
        // 我们必须 release() 释放所有权，防止 ulock 析构时自动解锁。
        ulock.release(); 
        
        return true;
    }

    // 超时等待
    bool timewait(std::mutex *m_mutex, struct timespec t)
    {
        std::unique_lock<std::mutex> ulock(*m_mutex, std::adopt_lock);
        
        // 将 timespec (绝对时间) 转换为 chrono 时间点
        auto duration = std::chrono::seconds(t.tv_sec) + std::chrono::nanoseconds(t.tv_nsec);
        auto time_point = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));

        std::cv_status status = m_cv.wait_until(ulock, time_point);
        
        ulock.release(); // 同样需要释放所有权
        
        return status == std::cv_status::no_timeout;
    }

    bool signal()
    {
        m_cv.notify_one();
        return true;
    }

    bool broadcast()
    {
        m_cv.notify_all();
        return true;
    }

private:
    std::condition_variable m_cv;
};

#endif
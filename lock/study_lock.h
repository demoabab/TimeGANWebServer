#include <exception>
#include <mutex>
#include <condition_variable>
#include <chrono>

class sem
{
public:
    sem(int num = 0) : m_count = num {}

    sem(const sem&) = delete;
    sem& operator=(const sem&) = delete;

    ~sem() = default;

    bool post(){
        std::unique_lock<std::mutex> lock(m_mutex);
        cv.wait(lock, [this](){return m_count>0; });
        m_count--;
        return true;
    }

    bool wait(){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_count++;
        cv.notify_one();
        return true;
    }

    void init(int num){
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count = num;
    }
private:
    int m_count;
    std::condition_variable cv;
    std::mutex m_mutex;
}

class locker
{
public:
    locker() = default;
    ~locker() = default;

    bool lock(){
        m_mutex.lock();
        return true;
    }

    bool unlock(){
        m_mutex.unlock();
        return true;
    }

    std::mutex *get(){
        return &m_mutex;
    }
private:
    std::mutex m_mutex;
}
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <thread>
#include <mutex>
#include <vector>

using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
    // [修改] 释放缓冲区内存
    if(m_buf) {
        delete[] m_buf;
    }
}

// [后端消费者线程] 核心双缓冲逻辑
void Log::async_write_log()
{
    while (true) // 这里的退出条件可以根据需要优化，比如加一个 stop 标志
    {
        // 1. 加锁等待
        std::unique_lock<std::mutex> ulock(m_mutex);
        
        // 2. 等待条件：输入缓冲区不为空，或者超时（比如 3秒强制刷新一次）
        // 这里的 wait_for 既解决了“有数据就写”，也解决了“没数据但时间到了也要刷盘”的问题
        m_cond.wait_for(ulock, std::chrono::seconds(3), [this] {
            return !m_input_buffer.empty(); 
        });

        if (m_input_buffer.empty()) {
            continue;
        }

        // 3. 【核心步骤】交换缓冲区 (Swap)
        // 这一步极快，只是交换了两个 vector 的指针/内部结构
        // 此时 m_input_buffer 变空了，前端可以继续写；
        // m_output_buffer 拿到了数据，准备写盘
        m_output_buffer.swap(m_input_buffer);
        
        // 4. 立刻解锁！
        // 后面的磁盘 IO 操作不需要持有锁
        ulock.unlock();

        // 5. 写入磁盘 (IO 操作，耗时)
        if (!m_output_buffer.empty()) {
            for (const auto& log_str : m_output_buffer) {
                fputs(log_str.c_str(), m_fp);
            }
            // 清空输出缓冲区，准备下一次交换
            m_output_buffer.clear();
            
            // 每次写完一批，强制刷新到底层文件系统
            fflush(m_fp);
        }
    }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        
        // [修改] 预留空间，避免频繁 reallocate
        m_input_buffer.reserve(1000);
        m_output_buffer.reserve(1000);

        // 创建线程
        std::thread t(flush_log_thread);
        t.detach();
    }
    
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
 
    const char *p = strrchr(file_name, '/');
    char log_full_name[512] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    
    // [修改] 使用 lock_guard 自动加锁
    std::unique_lock<std::mutex> ulock(m_mutex);
    m_count++;

    // 日志滚动逻辑 (分文件)
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) 
    {
        char new_log[512] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 511, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 511, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
 
    ulock.unlock(); // 暂时解锁，进行耗时的字符串格式化

    va_list valst;
    va_start(valst, format);

    string log_str;
    
    // 复用 lock (m_mutex)，因为要操作 m_buf
    ulock.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    // [核心修改]
    if (m_is_async)
    {
        // 放入输入缓冲区
        m_input_buffer.push_back(log_str);
        
        // 如果缓冲区积压太多，或者 urgency 比较高，可以立刻唤醒后端
        // 这里简单处理：只要有数据就保留锁，函数结束自动解锁
        // 但为了性能，只有当 buffer 达到一定量才 notify 也是一种策略
        // 为了实时性，我们这里选择如果有 1 条以上就 notify，
        // 但因为 lock 范围小，所以性能依然很高
        if (m_input_buffer.size() >= 1) {
             m_cond.notify_one();
        }
    }
    else
    {
        // 同步模式直接写
        fputs(log_str.c_str(), m_fp);
    }
    // ulock 析构自动解锁
    va_end(valst);
}

void Log::flush(void)
{
    // [修改] 强制唤醒后端线程刷盘
    std::unique_lock<std::mutex> ulock(m_mutex);
    // 这里其实应该通知后端把剩下的写完，但为了简化，我们只 flush 文件流
    fflush(m_fp); 
    // 更严谨的做法是 notify 后端
    m_cond.notify_one();
}
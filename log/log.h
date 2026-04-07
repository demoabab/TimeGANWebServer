#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
// #include <pthread.h> // [删除] 
// #include "block_queue.h"     移除阻塞队列，引入双缓冲区
#include <vector>
#include <mutex>
#include <condition_variable>

using namespace std;

class Log
{
public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    // [修改] 返回值改为 void，参数去掉（因为不需要传参了）
    static void flush_log_thread()
    {
        Log::get_instance()->async_write_log();
    }

    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void async_write_log();

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    // block_queue<string> *m_log_queue; //阻塞队列

    bool m_is_async; //是否同步标志位
    int m_close_log; //关闭日志

    std::vector<std::string> m_input_buffer;     //前端输入缓冲区
    std::vector<std::string> m_output_buffer;    //后端输出缓冲区

    std::mutex m_mutex;
    std::condition_variable m_cond;
};

// 宏定义保持不变
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
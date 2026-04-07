#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "../lock/memory_pool.h"
#include "../log/log.h"

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL), rotation(0), time_slot(0) {}

    void* operator new(size_t size){
        return m_pool.allocate();
    }

    void operator delete(void* p){
        m_pool.deallocate(p);
    }

public:
    time_t expire;                  // 任务的绝对超时时间
    
    void (*cb_func)(client_data *); // 任务回调函数
    client_data *user_data;         // 回调函数处理的客户数据
    
    util_timer *prev;               // 指向前一个定时器
    util_timer *next;               // 指向后一个定时器

    // [时间轮特有]
    int rotation;                   // 记录定时器在时间轮转多少圈后生效
    int time_slot;                  // 记录定时器属于时间轮上哪个槽

    static MemoryPool<util_timer> m_pool;
};

// [修改] 时间轮类，替代原来的 sort_timer_lst
class time_wheel
{
public:
    time_wheel();
    ~time_wheel();

    // 添加定时器：O(1)
    util_timer* add_timer(int timeout);
    
    // 删除定时器：O(1)
    void del_timer(util_timer *timer);
    
    // 调整定时器（在时间轮中，调整等于 删除+重新添加）：O(1)
    void adjust_timer(util_timer *timer);
    
    // 心跳函数：每秒调用一次
    void tick();

private:
    static const int N = 60;    // 时间轮上槽的数目
    static const int SI = 1;    // 每 1 秒时间轮转动一次
    util_timer *slots[N];       // 时间轮的槽，其中每个元素指向一个无序链表
    int cur_slot;               // 时间轮的当前槽
};

// Utils 类需要适配 time_wheel
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    time_wheel m_timer_lst; // [修改] 这里改成了 time_wheel
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
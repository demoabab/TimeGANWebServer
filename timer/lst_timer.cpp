#include "lst_timer.h"
#include "../http/http_conn.h"

//初始化静态内存池
MemoryPool<util_timer> util_timer::m_pool;


// --- 时间轮实现 ---
time_wheel::time_wheel() : cur_slot(0)
{
    for (int i = 0; i < N; ++i)
    {
        slots[i] = NULL;
    }
}

time_wheel::~time_wheel()
{
    for (int i = 0; i < N; ++i)
    {
        util_timer *tmp = slots[i];
        while (tmp)
        {
            slots[i] = tmp->next;
            delete tmp;
            tmp = slots[i];
        }
    }
}

// 添加定时器
// timeout: 多少秒后过期
util_timer* time_wheel::add_timer(int timeout)
{
    if (timeout < 0) return NULL;

    int ticks = 0;

    // 1. 根据 timeout 计算需要转动的滴答数
    // 如果 timeout 小于时间间隔 SI，则算作 1 个滴答
    if (timeout < SI)
    {
        ticks = 1;
    }
    else
    {
        ticks = timeout / SI;
    }

    // 2. 计算转动圈数
    int rotation = ticks / N;

    // 3. 计算槽位
    // 当前槽位 + 偏移量，对 N 取模
    int ts = (cur_slot + (ticks % N)) % N;

    // 4. 创建新的定时器节点
    util_timer *timer = new util_timer;
    timer->rotation = rotation;
    timer->time_slot = ts;
    
    // 记录绝对时间，方便外部使用（可选）
    time_t cur = time(NULL);
    timer->expire = cur + timeout;

    // 5. 头插法插入到 slots[ts] 链表中
    // 这一步是 O(1) 的关键！
    if (!slots[ts])
    {
        slots[ts] = timer;
    }
    else
    {
        timer->next = slots[ts];
        slots[ts]->prev = timer;
        slots[ts] = timer;
    }
    return timer;
}

void time_wheel::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    
    int ts = timer->time_slot;
    
    // 双向链表的常规删除操作
    if (timer == slots[ts])
    {
        slots[ts] = slots[ts]->next;
        if (slots[ts])
        {
            slots[ts]->prev = NULL;
        }
        delete timer;
    }
    else
    {
        timer->prev->next = timer->next;
        if (timer->next)
        {
            timer->next->prev = timer->prev;
        }
        delete timer;
    }
}

void time_wheel::adjust_timer(util_timer *timer)
{
    if (!timer) return;
    
    // 在时间轮中，调整定时器稍微麻烦一点：
    // 因为槽位是根据创建时间算出来的，仅仅修改 expire 不行。
    // 必须先把旧的删掉，再重新添加一个新的。
    
    // 1. 保存必要数据
    int timeout = 3 * 5; // 这里假设 TIMESLOT=5，延长3倍。
                         // 为了更通用，你可以传参进来，或者读取全局配置
                         // 在这里我们暂时硬编码为 15秒 (3*TIMESLOT) 
                         // *注意*: 你可能需要把 WebServer 中的 TIMESLOT 传进来
    
    // 实际上，更优雅的做法是在 WebServer 调用 adjust 的地方
    // 直接调 del_timer 然后再调 add_timer。
    // 但为了保持接口兼容，我们这里简单处理：
    
    // 由于我们无法直接获得原始的 timeout（expire是绝对时间），
    // 且 time_wheel 的设计通常是重新添加。
    // 这里的 adjust_timer 需要你稍微修改一下调用逻辑。
    // 为了不破坏 WebServer 的代码，我们采用 "假调整"：
    // 即：该函数只负责删除旧的，外部（WebServer）需要负责添加新的。
    
    // **重要修正**：
    // 原来的链表实现中，timer 对象是长期存在的。
    // 但在时间轮中，add_timer 会 new 一个新的。
    // 所以，我们不能简单地修改 timer 的属性。
    // 鉴于这个接口差异，我建议直接在 WebServer.cpp 里改逻辑，
    // 把 adjust_timer 废弃，改成 del + add。
    
    // 但为了编译通过，这里我们留空，或者只做删除。
    // 真正的逻辑修改在 webserver.cpp 中进行。
}

void time_wheel::tick()
{
    // 取出当前槽位的链表头
    util_timer *tmp = slots[cur_slot];
    
    while (tmp)
    {
        // 如果 rotation > 0，说明还没到这一圈，圈数减 1
        if (tmp->rotation > 0)
        {
            tmp->rotation--;
            tmp = tmp->next;
        }
        // 否则，说明定时器已到期
        else
        {
            // 执行回调函数
            tmp->cb_func(tmp->user_data);
            
            // 这一步是删除操作
            if (tmp == slots[cur_slot])
            {
                slots[cur_slot] = slots[cur_slot]->next;
                delete tmp;
                if (slots[cur_slot])
                {
                    slots[cur_slot]->prev = NULL;
                }
                tmp = slots[cur_slot];
            }
            else
            {
                tmp->prev->next = tmp->next;
                if (tmp->next)
                {
                    tmp->next->prev = tmp->prev;
                }
                util_timer *t2 = tmp->next;
                delete tmp;
                tmp = t2;
            }
        }
    }
    
    // 时间轮向前滚动一格
    cur_slot = ++cur_slot % N;
}

// --- Utils 类的实现 (部分保持不变) ---

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig)
{
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler()
{
    // 调用时间轮的 tick
    m_timer_lst.tick();
    
    // 重新设定闹钟
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
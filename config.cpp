#include "config.h"
#include <cstring>

Config::Config(){
    //端口号,默认9006
    PORT = 9006;

    //日志写入方式，默认同步
    LOGWrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    TRIGMode = 0;

    //listenfd触发模式，默认LT
    LISTENTrigmode = 0;

    //connfd触发模式，默认LT
    CONNTrigmode = 0;

    //优雅关闭链接，默认不使用
    OPT_LINGER = 0;

    //数据库连接池数量,默认8
    sql_num = 8;

    //线程池内的线程数量,默认8
    thread_num = 8;

    //关闭日志,默认不关闭
    close_log = 0;

    //并发模型,默认是proactor
    actor_model = 0;

    // Redis 默认配置
    strcpy(redis_host, "127.0.0.1");
    redis_port = 6379;
    redis_password[0] = '\0';
    session_ttl = 3600;   // 默认1小时
}

void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:R:r:w:T:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        case 'R':
        {
            strncpy(redis_host, optarg, sizeof(redis_host) - 1);
            break;
        }
        case 'r':
        {
            redis_port = atoi(optarg);
            break;
        }
        case 'w':
        {
            strncpy(redis_password, optarg, sizeof(redis_password) - 1);
            break;
        }
        case 'T':
        {
            session_ttl = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}
#include "config.h"
#include "http_conn.h"   // 新增：包含 init_model 声明
#include "redis_manager.h"

int g_session_ttl = 3600;  // 全局会话 TTL，由 main 设置

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "root";
    string databasename = "tinywebserver";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    // 新增：初始化模型（路径相对于可执行文件所在目录）
    // 请确保模型文件放在 build/exported_models/model_normal_0g.pt
    init_models();

    // 设置全局会话 TTL
    g_session_ttl = config.session_ttl;

    // 初始化 Redis 连接
    RedisManager* redis = RedisManager::getInstance();
    if (!redis->connect(config.redis_host, config.redis_port,
                        config.redis_password, 2000)) {
        printf("WARNING: Redis 连接失败，会话管理将不可用\n");
    }

    WebServer server;

    //初始化
    server.init(config.PORT, config.mysql_host, user, passwd, databasename,
                config.LOGWrite, config.OPT_LINGER, config.TRIGMode, config.sql_num,
                config.thread_num, config.close_log, config.actor_model);
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}
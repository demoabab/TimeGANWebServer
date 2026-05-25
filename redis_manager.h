#ifndef REDIS_MANAGER_H
#define REDIS_MANAGER_H

#include <string>
#include <hiredis/hiredis.h>
#include "lock/locker.h"

class RedisManager {
public:
    static RedisManager* getInstance();

    bool connect(const std::string& host, int port,
                 const std::string& password, int timeout_ms = 2000);
    void disconnect();
    bool isConnected();

    bool setSession(const std::string& token, const std::string& username, int ttl_seconds);
    bool getSession(const std::string& token, std::string& username);
    bool delSession(const std::string& token);

private:
    RedisManager() : m_ctx(nullptr) {}
    ~RedisManager() { disconnect(); }
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    bool reconnect();
    std::string safeCmd(const char* format, ...);

    redisContext* m_ctx;
    locker m_lock;

    std::string m_host;
    int m_port;
    std::string m_password;
    int m_timeout_ms;
};

#endif

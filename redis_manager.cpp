#include "redis_manager.h"
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <strings.h>

RedisManager* RedisManager::getInstance() {
    static RedisManager instance;
    return &instance;
}

bool RedisManager::connect(const std::string& host, int port,
                           const std::string& password, int timeout_ms) {
    m_host = host;
    m_port = port;
    m_password = password;
    m_timeout_ms = timeout_ms;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    m_ctx = redisConnectWithTimeout(host.c_str(), port, tv);
    if (m_ctx == nullptr || m_ctx->err) {
        if (m_ctx) {
            fprintf(stderr, "[Redis] connect failed: %s\n", m_ctx->errstr);
            redisFree(m_ctx);
            m_ctx = nullptr;
        } else {
            fprintf(stderr, "[Redis] connect failed: cannot allocate context\n");
        }
        return false;
    }

    redisSetTimeout(m_ctx, tv);

    if (!password.empty()) {
        redisReply* reply = (redisReply*)redisCommand(m_ctx, "AUTH %s", password.c_str());
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            fprintf(stderr, "[Redis] AUTH failed: %s\n",
                    reply ? reply->str : m_ctx->errstr);
            if (reply) freeReplyObject(reply);
            redisFree(m_ctx);
            m_ctx = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    printf("[Redis] connected: %s:%d\n", host.c_str(), port);
    return true;
}

void RedisManager::disconnect() {
    if (m_ctx) {
        redisFree(m_ctx);
        m_ctx = nullptr;
    }
}

bool RedisManager::isConnected() {
    return m_ctx != nullptr && m_ctx->err == 0;
}

bool RedisManager::reconnect() {
    disconnect();
    return connect(m_host, m_port, m_password, m_timeout_ms);
}

bool RedisManager::setSession(const std::string& token, const std::string& username, int ttl_seconds) {
    m_lock.lock();
    if (!isConnected() && !reconnect()) {
        m_lock.unlock();
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(m_ctx,
        "SETEX session:%s %d %s", token.c_str(), ttl_seconds, username.c_str());
    if (reply == nullptr) {
        fprintf(stderr, "[Redis] SETEX failed: %s\n", m_ctx->errstr);
        reconnect();
        m_lock.unlock();
        return false;
    }

    bool ok = (reply->type == REDIS_REPLY_STATUS &&
               strcasecmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    m_lock.unlock();
    return ok;
}

bool RedisManager::getSession(const std::string& token, std::string& username) {
    m_lock.lock();
    if (!isConnected() && !reconnect()) {
        m_lock.unlock();
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(m_ctx,
        "GET session:%s", token.c_str());
    if (reply == nullptr) {
        fprintf(stderr, "[Redis] GET failed: %s\n", m_ctx->errstr);
        reconnect();
        m_lock.unlock();
        return false;
    }

    bool found = (reply->type == REDIS_REPLY_STRING);
    if (found) {
        username.assign(reply->str, reply->len);
    }
    freeReplyObject(reply);
    m_lock.unlock();
    return found;
}

bool RedisManager::delSession(const std::string& token) {
    m_lock.lock();
    if (!isConnected() && !reconnect()) {
        m_lock.unlock();
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(m_ctx,
        "DEL session:%s", token.c_str());
    if (reply == nullptr) {
        fprintf(stderr, "[Redis] DEL failed: %s\n", m_ctx->errstr);
        reconnect();
        m_lock.unlock();
        return false;
    }

    bool deleted = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    m_lock.unlock();
    return deleted;
}

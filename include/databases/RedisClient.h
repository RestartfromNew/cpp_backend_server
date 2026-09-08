//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_REDISCLIENT_H
#define CPP_BACKEND_SERVER_REDISCLIENT_H
#include <sw/redis++/redis++.h>

/**
 * @brief Manages a Redis server
 *
 * @note DatabaseConnection owns a Redis pools and automatically release it when the object is destroyed
 * automatically when the object is destroyed.
 *
 * The connection is non-copyable or movable.
 */
class RedisClient {
    private:
    //理论上不需要这个options_
    // sw::redis::ConnectionOptions options_;
    sw::redis::Redis redisPool_;
    public:
    /**
     *
    * @param options sw::redis::ConnectionOptions options;
        options.host = "127.0.0.1";
        options.port = 6379;
     */
    explicit RedisClient(const sw::redis::ConnectionOptions& options);
    ~RedisClient();
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    RedisClient(RedisClient&&) = delete;
    RedisClient& operator=(RedisClient&&) = delete;

    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const std::string& value,std::chrono::seconds ttl);
    bool remove(const std::string& key);
    bool exists(const std::string& key);
};


#endif //CPP_BACKEND_SERVER_REDISCLIENT_H

//
// Created by yangb on 2026/9/4.
//

#include "../../include/databases/RedisClient.h"
#include "../../include/databases/RedisClient.h"
#include <sw/redis++/redis++.h>
RedisClient::RedisClient(const sw::redis::ConnectionOptions& options):redisPool_(options){};
RedisClient::~RedisClient(){};

std::optional<std::string>
RedisClient::get(const std::string& key) {
    auto val = redisPool_.get(key);
    return val;
}

void RedisClient::set(const std::string& key, const std::string& value) {
    redisPool_.set(key, value);
}
void RedisClient::set(const std::string& key, const std::string& value,std::chrono::seconds ttl) {
    redisPool_.set(key, value, ttl);
}
bool RedisClient::exists(const std::string& key) {
    return redisPool_.exists(key);
}
bool RedisClient:: remove(const std::string& key) {
    return redisPool_.del(key);
}

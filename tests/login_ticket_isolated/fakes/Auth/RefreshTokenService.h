#pragma once
#include <string>
#include <atomic>
struct RefreshTokenService{inline static std::atomic<int> count{0};std::string generateRefreshToken(){return "refresh:"+std::to_string(++count);}std::string RefreshTokenHash(const std::string& t){return "hashed:"+t;}};

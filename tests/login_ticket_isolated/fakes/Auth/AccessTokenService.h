#pragma once
#include <string>
struct AccessTokenService{std::string generateAccessToken(const std::string& user){return "access:"+user;}};

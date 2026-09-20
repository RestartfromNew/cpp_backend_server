//
// Created by yangb on 2026/9/16.
//

#ifndef CPP_BACKEND_SERVER_ACCESSTOKENSERVICE_H
#define CPP_BACKEND_SERVER_ACCESSTOKENSERVICE_H

#include <jwt-cpp/jwt.h>
#include <boost/uuid/uuid_io.hpp>
#include <cstdlib>
#include <stdexcept>
#include <variant>
#include <string>
#include <boost/uuid/string_generator.hpp>
enum class AccessTokenError {
    Invalid,
    Expired
};

struct AuthIdentity {
    boost::uuids::uuid userId;
};

using AccessTokenResult =
    std::variant<AuthIdentity, AccessTokenError>;
class AccessTokenService {
public:
    AccessTokenService(const std::string &secret);
    ~AccessTokenService()=default;
    std::string generateAccessToken(const std::string& userId);
    AccessTokenResult verify(const std::string& accessToken) const;
private:
    std::string secret_;
};


#endif //CPP_BACKEND_SERVER_ACCESSTOKENSERVICE_H

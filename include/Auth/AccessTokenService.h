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

/** @brief JWT 验证得到的用户身份；不是用户自行提交的可信 user_id，也不证明设备仍有效。 */
struct AuthIdentity {
    boost::uuids::uuid userId;
};

using AccessTokenResult =
    std::variant<AuthIdentity, AccessTokenError>;
/** @brief 签发与本地验证 HS256 JWT；不查询设备状态。调用者另行绑定 Session 身份，密钥与 Token 不得写日志。 */
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

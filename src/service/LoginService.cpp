#include "common/Logger.h"
//
// Created by yangb on 2026/9/16.
//

#include "service/LoginService.h"
#include <sodium/crypto_pwhash.h>
LoginService::LoginService(UserRepository& userRepository, LoginTicketService& tickets)
    : userRepository_(userRepository), tickets_(tickets) {}
bool LoginService::verifyPassword( const std::string &password_hash, const std::string &password ) {
    //password的hash会生成随机盐
    return crypto_pwhash_str_verify(password_hash.c_str(), // 数据库存储的哈希字符串
    password.data(),       // 用户本次输入的原始密码
    password.size()        // 密码字节数
    ) == 0;
}

LoginServiceResult LoginService::loginByEmail(const std::string &email, const std::string &password) {
    backend::logging::Scope operationLog{"LoginService", "loginByEmail"};
    //默认传入的时候格式已经正确
    auto password_login_record=userRepository_.FindLoginRecordByEmail(email);
    if (!password_login_record)
        //该用户不存在
        return LoginServiceResult{LoginError::NoUserFound};
    //验证密码
    if (!verifyPassword(password_login_record->password_hash, password))
        return LoginServiceResult{LoginError::InvalidCredentials};
    //验证是否是激活状态
    if (!password_login_record->is_active)
        return LoginServiceResult{LoginError::NotActiveUser};
    auto ticket = tickets_.issue(password_login_record->id);
    if (!ticket) return LoginServiceResult{LoginError::NotActiveUser};
    backend::logging::Logger::write(backend::logging::Level::Info, "login", "ticket_issued");
    return LoginServiceResult{LoginError::Success, std::move(ticket)};
}

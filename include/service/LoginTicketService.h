#pragma once
#include "repository/LoginTicketRepository.h"
#include "Auth/AccessTokenService.h"
#include "Auth/RefreshTokenService.h"
/** @brief 协调一次性 ticket 与设备登录；数据库完成提交后生成 Access Token，响应丢失需按 API 重试规则处理。 */
class LoginTicketService {
public:
    LoginTicketService(LoginTicketRepository& repository, AccessTokenService& access,
        RefreshTokenService& refresh) : repository_(repository), access_(access), refresh_(refresh) {}
    std::optional<std::string> issue(const boost::uuids::uuid& user_id);
    // 两完成入口共用一次性 ticket；失败按 LoginTicketStatus 返回，基础设施异常向 HTTP 边界传播。
    CompletedLogin verifyDevice(const std::string& ticket, const boost::uuids::uuid& device);
    CompletedLogin registerDevice(const std::string& ticket, const DeviceRegistrationInput& input);
private:
    CompletedLogin complete(const std::string& ticket, const boost::uuids::uuid* device,
        const DeviceRegistrationInput* input);
    static bool validTicket(const std::string& ticket);
    std::string hashTicket(const std::string& ticket);
    LoginTicketRepository& repository_;
    AccessTokenService& access_;
    RefreshTokenService& refresh_;
};

#pragma once
#include "http/HttpResponse.h"
#include "domain/LoginTicket.h"
#include <boost/uuid/uuid_io.hpp>
#include <stdexcept>
// 协议边界集中映射完成登录结果；响应含 Token，日志只记录 HTTP 状态。
inline HttpResponse loginTicketResponse(const CompletedLogin& login, bool registered = false) {
    switch (login.result.status) {
        case LoginTicketStatus::InvalidTicket:
            return ErrorResponseMaker(HttpStatus::Unauthorized, "invalid_login_ticket", "Login ticket is invalid, expired or already used");
        case LoginTicketStatus::AccountDisabled:
            return ErrorResponseMaker(HttpStatus::Forbidden, "account_disabled", "Account is disabled");
        case LoginTicketStatus::DeviceNotFound:
            return ErrorResponseMaker(HttpStatus::Not_Found, "device_not_found", "Device is not registered");
        case LoginTicketStatus::DeviceNotOwned:
            return ErrorResponseMaker(HttpStatus::Forbidden, "device_not_owned", "Device does not belong to this login");
        case LoginTicketStatus::DeviceNotActive:
            return ErrorResponseMaker(HttpStatus::Forbidden, "device_not_active", "Device is not active");
        case LoginTicketStatus::Success: break;
    }
    if (!login.result.identity) throw std::logic_error("Missing login identity");
    HttpResponse response;
    response.status = registered ? HttpStatus::Created : HttpStatus::Ok;
    response.headers["Content-Type"] = "application/json";
    response.body = nlohmann::json{
        {"id", boost::uuids::to_string(login.result.identity->user_id)},
        {"device_id", boost::uuids::to_string(login.result.identity->device_id)},
        {"device_status", "active"}, {"access_token", login.access_token},
        {"refresh_token", login.refresh_token}}.dump();
    return response;
}

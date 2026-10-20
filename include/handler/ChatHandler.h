//
// Created by yangb on 2026/10/8.
//

#ifndef CPP_BACKEND_SERVER_CHATHANDLER_H
#define CPP_BACKEND_SERVER_CHATHANDLER_H

#include "service/KeyService.h"
#include "service/LoginTicketService.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
/** @brief 设备及公钥 HTTP 协议边界；登记以 ticket 认证，其余接口按各路由认证约定调用 Service。 */
class ChatHandler {
public:
    ChatHandler(KeyService& keyService, LoginTicketService& tickets);
    HttpResponse registerNewDevice(const HttpRequest& request);

    HttpResponse uploadOneTimePrekeys(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse getOneTimePrekey(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse fetchUserDevices(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse getDeviceKeyBundle(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    private:
    KeyService& keyService_;
    LoginTicketService& tickets_;
};


#endif //CPP_BACKEND_SERVER_CHATHANDLER_H

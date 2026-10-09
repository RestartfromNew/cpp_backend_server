//
// Created by yangb on 2026/10/8.
//

#ifndef CPP_BACKEND_SERVER_CHATHANDLER_H
#define CPP_BACKEND_SERVER_CHATHANDLER_H

#include "service/KeyService.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
class ChatHandler {
public:
    explicit ChatHandler(KeyService& keyService);
    HttpResponse registerNewDevice(const HttpRequest& request,const boost::uuids::uuid&);

    HttpResponse uploadOneTimePrekeys(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse getOneTimePrekey(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse fetchFriendDevices(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    HttpResponse getDeviceKeyBundle(const HttpRequest& request,
        const boost::uuids::uuid& user_id);
    private:
    KeyService& keyService_;
};


#endif //CPP_BACKEND_SERVER_CHATHANDLER_H

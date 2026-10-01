//
// Created by yangb on 2026/9/11.
//
#pragma once


#include "handler/Router.h"
#include "http/HttpParse.h"
#include "string"
#include "server/Connection.h"
#include "http/HttpResponseGenerator.h"
#include "common/UniqueFd.h"
#include "http/HttpParse.h"
#include "http/HttpResponse.h"
#include "databases/DatabaseError.h"
#include <boost/uuid/uuid_io.hpp>
#include "handler/Router.h"
#include <iostream>
#include "websocket/WebSocketPool.h"
#include "websocket/WebsocketWorker.h"
class HttpSession {
    private:
    Connection connection_;
    HttpParse parser_;
    Router &router_;

    public:
    HttpSession(Connection &&connection, Router &router);
    ~HttpSession()=default;
    std::optional<PendingWebsocket> HandleHttpSession();
    bool is_open();
    void sendAll(HttpResponse &response);

};



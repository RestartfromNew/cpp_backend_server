//
// Created by yangb on 2026/9/10.
//

#include "http/HttpResponse.h"
HttpResponse ErrorResponseMaker(HttpStatus status,std::string_view code,std::string_view message) {
    nlohmann::json body = {
        {
            "error",{{"code", code},
                    {"message", message}}
        }
    };
    HttpResponse response;
    response.status = status;
    response.headers["Content-Type"] ="application/json";
    response.headers["Connection"] ="Close";
    response.body = body.dump();
    return response;
}
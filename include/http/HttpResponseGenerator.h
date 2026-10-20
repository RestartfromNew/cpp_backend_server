//
// Created by yangb on 2026/9/9.
//

#ifndef CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H
#define CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H
#include "http/HttpResponse.h"
#include "nlohmann/json.hpp"

/** @brief 将 HttpResponse 编码为 HTTP 字节；不做身份判断，也不保证响应 body 可安全写日志。 */
class HttpResponseGenerator {
    public:
    HttpResponseGenerator()=default;
    ~HttpResponseGenerator()=default;
    std::string generateHttpResponse(const HttpResponse &response) ;


};


#endif //CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H

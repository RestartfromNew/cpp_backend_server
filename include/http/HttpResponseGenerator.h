//
// Created by yangb on 2026/9/9.
//

#ifndef CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H
#define CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H
#include "http/HttpResponse.h"
#include "nlohmann/json.hpp"

class HttpResponseGenerator {
    public:
    HttpResponseGenerator()=default;
    ~HttpResponseGenerator()=default;
    std::string generateHttpResponse(const HttpResponse &response) ;
};


#endif //CPP_BACKEND_SERVER_HTTPRESPONSEGENERATOR_H

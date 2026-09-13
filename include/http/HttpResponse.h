//
// Created by yangb on 2026/9/9.
//

#ifndef CPP_BACKEND_SERVER_HTTPRESPONSE_H
#define CPP_BACKEND_SERVER_HTTPRESPONSE_H
#include <string>
#include "unordered_map"
#include "nlohmann/json.hpp"
enum class HttpStatus {
    Ok = 200,
    Created = 201,
    Accepted = 202,
    No_Content=204,
    Bad_Request = 400,
    Forbidden = 403,
    Not_Found = 404,
    Method_Not_Allowed = 405,
    Request_Timeout = 408,
    Internal_Server_Error = 500,
    Service_Unavailable = 503,
    Bad_Gateway = 502,
};
constexpr const char* reasonPhrase(HttpStatus status)
{
    switch (status) {
        case HttpStatus::Ok:
            return "OK";
        case HttpStatus::Created:
            return "Created";
        case HttpStatus::Accepted:
            return "Accepted";
        case HttpStatus::No_Content:
            return "No Content";
        case HttpStatus::Bad_Request:
            return "Bad Request";
        case HttpStatus::Forbidden:
            return "Forbidden";
        case HttpStatus::Not_Found:
            return "Not Found";
        case HttpStatus::Method_Not_Allowed:
            return "Method Not Allowed";
        case HttpStatus::Request_Timeout:
            return "Request Timeout";
        case HttpStatus::Internal_Server_Error:
            return "Internal Server Error";
        case HttpStatus::Bad_Gateway:
            return "Bad Gateway";
        case HttpStatus::Service_Unavailable:
            return "Service Unavailable";
    }

    return "Unknown";
}

struct HttpResponse {
    HttpStatus status{HttpStatus::Ok};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};
HttpResponse ErrorResponseMaker(HttpStatus status,std::string_view code,std::string_view message);
#endif //CPP_BACKEND_SERVER_HTTPRESPONSE_H

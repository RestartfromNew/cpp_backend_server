//
// Created by yangb on 2026/9/8.
//
#pragma once
#include <unordered_map>
#include <string>


enum class HttpMethod {
    GET,POST,PUT,DELETE,PATCH,HEAD,OPTIONS,TRACE,CONNECT,UNKNOWN
};
struct HttpRequest {
  HttpMethod method{HttpMethod::UNKNOWN};
    std::string host;
    std::string path;
    std::string version;
    std::unordered_map<std::string,std::string> headers;
    std::string body;
};


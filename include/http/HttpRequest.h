//
// Created by yangb on 2026/9/8.
//
#pragma once
#include <unordered_map>
#include <string>


enum class HttpMethod {
    GET,POST,PUT,DELETE,PATCH,HEAD,OPTIONS,TRACE,CONNECT,UNKNOWN
};
/** @brief HTTP 解析结果与自有字符串；头字段可能含认证凭据，不得整体 dump。HTTP 头大小写由使用方处理。 */
struct HttpRequest {
  HttpMethod method{HttpMethod::UNKNOWN};
    std::string host;
    std::string path;
    std::string version;
    std::unordered_map<std::string,std::string> headers;
    std::string body;
};


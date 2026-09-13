//
// Created by yangb on 2026/9/9.
//

#include "http/HttpResponseGenerator.h"

std::string HttpResponseGenerator::generateHttpResponse(const HttpResponse &response) {
    std::string result;
    result+="HTTP/1.1 ";
    const auto statusCode =
    static_cast<unsigned int>(response.status);
    result += std::to_string(statusCode);
    result += " ";
    result += reasonPhrase(response.status);
    result+="\r\n";
    for (auto i: response.headers) {
        result+=i.first;
        result+=": ";
        result+=i.second;
        result+="\r\n";
    }
    if (response.body.length() > 0) {
        result += "Content-Length: ";
        result += std::to_string(response.body.size());
        result += "\r\n";
    }
    result+="\r\n";
    result+=response.body;
    return result;
}


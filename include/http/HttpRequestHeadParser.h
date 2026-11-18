#pragma once

#include "http/HttpRequest.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class HttpRequestHeadStatus { NeedMoreData, Complete, Error };

struct HttpRequestHeadResult {
    HttpRequestHeadStatus status = HttpRequestHeadStatus::NeedMoreData;
    HttpRequest request;
    std::uint64_t content_length = 0; // 未声明 Content-Length 时为 0，可检查 headers 区分。
    std::size_t header_bytes = 0; // 包含末尾的 CRLF CRLF；后面的字节属于正文。
    int http_status = 400;
    std::string error_code;
};

// 只解析内存中的 HTTP 请求头，不读连接 socket，不收集完整正文，也不执行业务校验。
class HttpRequestHeadParser {
public:
    static constexpr std::size_t max_header_bytes = 16 * 1024;
    [[nodiscard]] static HttpRequestHeadResult parse(std::string_view input);
};

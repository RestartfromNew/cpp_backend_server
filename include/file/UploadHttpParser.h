#pragma once

#include "http/HttpRequest.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class UploadHeaderStatus { NeedMoreData, Complete, Error };

struct UploadHeaderResult {
    UploadHeaderStatus status = UploadHeaderStatus::NeedMoreData;
    HttpRequest request;
    std::uint64_t content_length = 0; // 客户端声明的正文长度，不代表 Service 批准大小。
    std::size_t header_bytes = 0; // 包含末尾的 CRLF CRLF；后面的字节属于正文。
    int http_status = 400;
    std::string error_code;
};

// 只解析内存中的 HTTP 请求头，不读连接 socket，不收集完整正文，也不验证上传权限。
class UploadHttpParser {
public:
    static constexpr std::size_t max_header_bytes = 16 * 1024;
    [[nodiscard]] static UploadHeaderResult parse(std::string_view input);
};

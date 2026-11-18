#pragma once

#include "http/HttpRequestHeadParser.h"

using UploadHeaderStatus = HttpRequestHeadStatus;
using UploadHeaderResult = HttpRequestHeadResult;

// 上传入口的请求头规则及原有错误码。
class UploadHttpParser {
public:
    static constexpr std::size_t max_header_bytes = HttpRequestHeadParser::max_header_bytes;
    [[nodiscard]] static UploadHeaderResult parse(std::string_view input);
};

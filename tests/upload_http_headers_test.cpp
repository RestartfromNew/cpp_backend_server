#include "file/UploadHttpParser.h"
#include <iostream>
#include <stdexcept>

int main() {
    int checks = 0;
    const auto require = [&](bool condition) {
        ++checks;
        if (!condition) throw std::runtime_error("upload header check failed: " + std::to_string(checks));
    };
    const std::string valid = "PUT /upload_file HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nContent-Length: 5\r\nAuthorization: Bearer test\r\nX-Upload-ID: test-id\r\nX-Upload-Token: test-token\r\n\r\n";
    // 每个分包位置都不能提前宣告请求头完整，包括末尾的 CRLF CRLF 被拆开。
    for (std::size_t length = 0; length < valid.size(); ++length) require(UploadHttpParser::parse(std::string_view{valid}.substr(0, length)).status == UploadHeaderStatus::NeedMoreData);
    auto parsed = UploadHttpParser::parse(valid + "hello");
    require(parsed.status == UploadHeaderStatus::Complete && parsed.header_bytes == valid.size());
    require(parsed.content_length == 5 && parsed.request.body.empty());
    require(parsed.request.headers.at("authorization") == "Bearer test");
    require(parsed.request.headers.at("x-upload-token") == "test-token");
    const auto request = [](const std::string& extra) { return "PUT /upload_file HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\n" + extra + "\r\n"; };
    require(UploadHttpParser::parse(request("Content-Length: 5\r\ncontent-length: 5\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: 5\r\nTransfer-Encoding: chunked\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: -1\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: +5\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: 18446744073709551616\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: 0\r\n")).status == UploadHeaderStatus::Error);
    auto without_length = UploadHttpParser::parse(request(""));
    require(without_length.status == UploadHeaderStatus::Complete && !without_length.request.headers.contains("content-length"));
    require(UploadHttpParser::parse(request("Content-Length: 5\r\nExpect: 100-continue\r\n")).http_status == 417);
    require(UploadHttpParser::parse(request("Content-Length: 5\r\nContent-Encoding: gzip\r\n")).http_status == 415);
    require(UploadHttpParser::parse(request("Content-Length: 5\r\nBad Name: value\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: 5\r\n Folded: value\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(request("Content-Length: 5\r\nX-Test: value\nother\r\n")).status == UploadHeaderStatus::Error);
    require(UploadHttpParser::parse(std::string(UploadHttpParser::max_header_bytes, 'x')).http_status == 431);
    auto get = UploadHttpParser::parse("GET /upload_file HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(get.status == UploadHeaderStatus::Complete && get.request.method == HttpMethod::GET);
    auto other_path = UploadHttpParser::parse("PUT /other HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(other_path.status == UploadHeaderStatus::Complete && other_path.request.path == "/other");
    require(UploadHttpParser::parse("PUT /upload_file HTTP/1.0\r\nHost: localhost\r\n\r\n").http_status == 505);
    require(UploadHttpParser::parse("PUT /upload_file HTTP/1.1\r\nContent-Length: 5\r\n\r\n").http_status == 400);
    // 请求头总长受限，但同包正文不应被当作请求头计入限制。
    require(UploadHttpParser::parse(valid + std::string(20000, 'x')).status == UploadHeaderStatus::Complete);
    std::cout << "PASS " << checks << " upload header checks\n";
}

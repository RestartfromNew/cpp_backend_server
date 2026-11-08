#include "file/UploadHttpParser.h"
#include <charconv>
#include <utility>

namespace {
UploadHeaderResult reject(int status, std::string code) {
    UploadHeaderResult result;
    result.status = UploadHeaderStatus::Error;
    result.http_status = status;
    result.error_code = std::move(code);
    return result;
}

bool isHeaderNameCharacter(unsigned char character) {
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')) return true;
    return std::string_view{"!#$%&'*+-.^_`|~"}.find(static_cast<char>(character)) != std::string_view::npos;
}

HttpMethod parseMethod(std::string_view method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "PATCH") return HttpMethod::PATCH;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "TRACE") return HttpMethod::TRACE;
    if (method == "CONNECT") return HttpMethod::CONNECT;
    return HttpMethod::UNKNOWN;
}

std::string_view trimWhitespace(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}
}

UploadHeaderResult UploadHttpParser::parse(std::string_view input) {
    // TCP 不保留请求边界；没有完整请求头时，调用者保留缓冲区，等待下一次连接 socket 可读。
    const auto ending = input.find("\r\n\r\n");
    if (ending == std::string_view::npos) {
        if (input.size() >= max_header_bytes) return reject(431, "upload_headers_too_large");
        return {};
    }
    const auto header_bytes = ending + 4;
    if (header_bytes > max_header_bytes) return reject(431, "upload_headers_too_large");

    const auto first_line_end = input.find("\r\n");
    if (first_line_end > 4096) return reject(400, "invalid_upload_request_line");
    const auto line = input.substr(0, first_line_end);
    const auto first_space = line.find(' ');
    const auto second_space = first_space == std::string_view::npos ? first_space : line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos || line.find(' ', second_space + 1) != std::string_view::npos) return reject(400, "invalid_upload_request_line");
    const auto method = line.substr(0, first_space);
    const auto path = line.substr(first_space + 1, second_space - first_space - 1);
    if (method.empty() || path.empty()) return reject(400, "invalid_upload_request_line");
    for (unsigned char character : method) {
        if (!isHeaderNameCharacter(character)) return reject(400, "invalid_upload_request_line");
    }
    for (unsigned char character : path) {
        if (character <= 32 || character == 127) return reject(400, "invalid_upload_request_line");
    }
    if (line.substr(second_space + 1) != "HTTP/1.1") return reject(505, "upload_http_version_not_supported");

    UploadHeaderResult result;
    result.request.method = parseMethod(method);
    result.request.path = path;
    result.request.version = "HTTP/1.1";
    std::size_t position = first_line_end + 2;
    std::size_t header_count = 0;
    while (position < ending + 2) {
        const auto line_end = input.find("\r\n", position);
        const auto header = input.substr(position, line_end - position);
        const auto colon = header.find(':');
        if (++header_count > 100) return reject(431, "too_many_upload_headers");
        if (colon == std::string_view::npos || colon == 0) return reject(400, "invalid_upload_header");

        std::string name{header.substr(0, colon)};
        for (char& character : name) {
            if (!isHeaderNameCharacter(static_cast<unsigned char>(character))) return reject(400, "invalid_upload_header");
            if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
        }
        const auto value = trimWhitespace(header.substr(colon + 1));
        for (unsigned char character : value) {
            if ((character < 32 && character != '\t') || character == 127) return reject(400, "invalid_upload_header");
        }
        // 上传入口采用严格子集：重复头一律拒绝，尤其不能让多个 Content-Length 产生歧义。
        if (!result.request.headers.emplace(std::move(name), std::string{value}).second) return reject(400, "duplicate_upload_header");
        position = line_end + 2;
    }

    const auto& headers = result.request.headers;
    const auto host = headers.find("host");
    if (host == headers.end() || host->second.empty()) return reject(400, "missing_upload_host");
    if (headers.contains("transfer-encoding")) return reject(400, "upload_transfer_encoding_not_supported");
    if (headers.contains("expect")) return reject(417, "upload_expect_not_supported");
    const auto encoding = headers.find("content-encoding");
    if (encoding != headers.end() && encoding->second != "identity") return reject(415, "upload_content_encoding_not_supported");
    const auto length = headers.find("content-length");
    if (length != headers.end()) {
        const auto& text = length->second;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result.content_length);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result.content_length == 0) return reject(400, "invalid_upload_content_length");
    }

    // Authorization、X-Upload-ID、X-Upload-Token 等原样保留，交给后续 Service 检查；绝不打印这些值。
    result.request.host = host->second;
    result.header_bytes = header_bytes;
    result.status = UploadHeaderStatus::Complete;
    return result;
}

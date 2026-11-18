#include "file/UploadHttpParser.h"

namespace {
constexpr std::string_view upload_errors[][2] = {
    {"http_headers_too_large", "upload_headers_too_large"},
    {"invalid_http_request_line", "invalid_upload_request_line"},
    {"http_version_not_supported", "upload_http_version_not_supported"},
    {"too_many_http_headers", "too_many_upload_headers"},
    {"invalid_http_header", "invalid_upload_header"},
    {"duplicate_http_header", "duplicate_upload_header"},
    {"missing_http_host", "missing_upload_host"},
    {"http_transfer_encoding_not_supported", "upload_transfer_encoding_not_supported"},
    {"invalid_http_content_length", "invalid_upload_content_length"},
};

UploadHeaderResult reject(int status, const char* code) {
    UploadHeaderResult result;
    result.status = UploadHeaderStatus::Error;
    result.http_status = status;
    result.error_code = code;
    return result;
}
}

UploadHeaderResult UploadHttpParser::parse(std::string_view input) {
    auto result = HttpRequestHeadParser::parse(input);
    if (result.status == UploadHeaderStatus::Error) {
        for (const auto& [common_code, upload_code] : upload_errors) {
            if (result.error_code == common_code) {
                result.error_code = upload_code;
                break;
            }
        }
        return result;
    }
    if (result.status != UploadHeaderStatus::Complete) return result;

    const auto& headers = result.request.headers;
    if (headers.contains("expect")) return reject(417, "upload_expect_not_supported");
    const auto encoding = headers.find("content-encoding");
    if (encoding != headers.end() && encoding->second != "identity") return reject(415, "upload_content_encoding_not_supported");
    if (headers.contains("content-length") && result.content_length == 0) return reject(400, "invalid_upload_content_length");
    return result;
}

#pragma once

#include <array>
#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

struct UploadPermissionRequest {
    boost::uuids::uuid client_request_id{};
    std::int64_t expected_size_bytes = 0;
    std::optional<std::array<std::uint8_t, 32>> expected_sha256;
};

struct UploadPermissionCredentials {
    std::string upload_url;
    std::string upload_token;
    std::int64_t credential_version = 1;
};

struct UploadPermissionGranted {
    boost::uuids::uuid attachment_id;
    boost::uuids::uuid upload_id;
    std::string upload_state;
    std::string expires_at;
    // 查询已有任务时，可能只返回任务状态，或提示客户端续签。
    std::optional<UploadPermissionCredentials> credentials;
    bool requires_renew = false;
};

struct UploadPermissionError {
    std::string code;
    std::string message;
};

struct UploadPermissionResponse {
    // request_id 无法解析时，错误响应可以省略此字段。
    std::optional<boost::uuids::uuid> request_id;
    std::variant<UploadPermissionError, UploadPermissionGranted> result;
};

//
// Created by yangb on 2026/11/11.
//

#include "service/FileService.h"
#include "common/Logger.h"
#include "databases/DatabaseError.h"
#include <array>
#include <limits>
#include <sodium.h>
#include <string_view>
#include <utility>

namespace {

        std::string generateUploadToken() {
                std::array<unsigned char, 32> random_bytes{};
                randombytes_buf(random_bytes.data(), random_bytes.size());

                std::array<char, 65> hex{};
                sodium_bin2hex(hex.data(), hex.size(), random_bytes.data(), random_bytes.size());

                return std::string{hex.data(), hex.size() - 1};
        }
        std::string hashUploadToken(std::string_view upload_token) {
                std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
                crypto_hash_sha256(
                    digest.data(),
                    reinterpret_cast<const unsigned char*>(upload_token.data()),
                    upload_token.size()
                );

                std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
                sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
                return std::string{hex.data(), hex.size() - 1};
        }

}
FileService::FileService(FileRepository& fileRepository) : fileRepository_(fileRepository) {}

VerifyPermissionResult FileService::verifyUploadPermission(const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& upload_id, const std::string& upload_token, std::uint64_t content_length) {
    if (user_id.is_nil()) return VerifyPermissionResult::Unauthorized;
    if (upload_id.is_nil()) return VerifyPermissionResult::InvalidUploadId;
    if (upload_token.empty()) return VerifyPermissionResult::MissingUploadToken;
    if (upload_token.size() != 64 || upload_token.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return VerifyPermissionResult::InvalidUploadTokenFormat;
    }
    if (content_length == 0 || content_length > std::numeric_limits<std::int64_t>::max()) {
        return VerifyPermissionResult::InvalidContentLength;
    }
    return fileRepository_.verifyUploadPermission(user_id, upload_id, hashUploadToken(upload_token), content_length);
}

std::variant<UploadPermissionError, UploadPermissionGranted> FileService::createUploadCredentials(
    const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id, const UploadPermissionRequest& request) {
    backend::logging::Scope operationLog{"FileService", "createUploadCredentials"};
    auto token = generateUploadToken();
    const auto token_hash = hashUploadToken(token);

    try {
        auto result = fileRepository_.createFileUploadPermission(user_id, device_id, token_hash, request);
        // 只有刚提交的新任务带凭据。幂等重试只返回已有任务的状态。
        if (auto* granted = std::get_if<UploadPermissionGranted>(&result); granted && granted->credentials) {
            granted->credentials->upload_token = std::move(token);
        }
        return result;
    } catch (const DatabaseError& error) {
        const auto code = error.code();
        backend::logging::Logger::write(backend::logging::Level::Error, "FileService", "upload_permission_database_failed",
            {{"sqlstate", code}});
        if (error.kind() == DatabaseErrorKind::Connection || code.starts_with("08")) {
            return UploadPermissionError{"database_unavailable", "Retry with the same request ID"};
        }
        if (code == "40001" || code == "40P01") {
            return UploadPermissionError{"retry_later", "Retry with the same request ID"};
        }
        return UploadPermissionError{"upload_permission_failed", "Unable to create upload permission"};
    }
}

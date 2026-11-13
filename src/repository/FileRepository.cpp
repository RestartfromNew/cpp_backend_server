//
// Created by yangb on 2026/11/11.
//

#include "repository/FileRepository.h"

#include "common/Logger.h"
#include "databases/ConnectionLease.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sodium.h>

FileRepository::FileRepository(DatabasePool& pool) : pool_(pool) {}

std::variant<VerifyPermissionResult, ClaimedUpload> FileRepository::claimUpload(const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& upload_id, const std::string& token_hash_hex, std::uint64_t content_length) {
    const std::string parameters[] = {boost::uuids::to_string(user_id), boost::uuids::to_string(upload_id),
        token_hash_hex, std::to_string(content_length)};
    ConnectionLease lease{pool_};
    auto& db = lease.connection();
    return db.withTransaction([&]() -> std::variant<VerifyPermissionResult, ClaimedUpload> {
        const auto owner = db.execute(R"(
            SELECT ud.device_id
            FROM app.file_uploads f JOIN app.users u ON u.id = f.uploader_id
            JOIN app.user_devices ud ON ud.device_id = f.uploader_device_id AND ud.user_id = u.id
            WHERE f.id = $2::uuid AND u.id = $1::uuid AND u.is_active AND ud.status = 'active'
            FOR SHARE OF ud, u
        )", std::span{parameters}.first<2>());
        if (owner.rowCount() == 0) return VerifyPermissionResult::Unauthorized;
        const auto claimed = db.execute(R"(
            UPDATE app.file_uploads f SET state = 'receiving', updated_at = clock_timestamp()
            FROM app.attachment a
            WHERE f.id = $2::uuid AND f.uploader_id = $1::uuid AND f.attachment_id = a.id
              AND a.status = 'uploading' AND f.state = 'authorized' AND f.expires_at > clock_timestamp()
              AND f.upload_token_hash = decode($3, 'hex') AND f.expected_size_bytes = $4::bigint
            RETURNING a.id, a.storage_key, COALESCE(encode(f.expected_sha256, 'hex'), '')
        )", parameters);
        if (claimed.rowCount() == 0) {
            const auto length_mismatch = db.execute(R"(
                SELECT 1 FROM app.file_uploads
                WHERE id = $2::uuid AND uploader_id = $1::uuid AND state = 'authorized'
                  AND expires_at > clock_timestamp() AND upload_token_hash = decode($3, 'hex')
                  AND expected_size_bytes <> $4::bigint
            )", parameters);
            return length_mismatch.rowCount() ? VerifyPermissionResult::ContentLengthMismatch : VerifyPermissionResult::Unauthorized;
        }
        return ClaimedUpload{upload_id, boost::uuids::string_generator{}(std::string{claimed.value(0, 0)}),
            std::string{claimed.value(0, 1)}, std::string{claimed.value(0, 2)}};
    });
}

bool FileRepository::finishUpload(const boost::uuids::uuid& upload_id, std::uint64_t size, const std::string& digest) {
    const std::string parameters[] = {boost::uuids::to_string(upload_id), std::to_string(size), digest};
    ConnectionLease lease{pool_};
    auto& db = lease.connection();
    return db.withTransaction([&] {
        const auto owner = db.execute(R"(
            SELECT ud.device_id
            FROM app.file_uploads f JOIN app.users u ON u.id = f.uploader_id
            JOIN app.user_devices ud ON ud.device_id = f.uploader_device_id AND ud.user_id = u.id
            WHERE f.id = $1::uuid AND u.is_active AND ud.status = 'active'
            FOR SHARE OF ud, u
        )", std::span{parameters}.first<1>());
        if (owner.rowCount() == 0) return false;
        const auto task = db.execute(R"(
            UPDATE app.file_uploads SET state = 'completed', finished_at = clock_timestamp(), updated_at = clock_timestamp()
            WHERE id = $1::uuid AND state = 'receiving' AND expires_at > clock_timestamp()
              AND expected_size_bytes = $2::bigint
              AND (expected_sha256 IS NULL OR expected_sha256 = decode($3, 'hex'))
            RETURNING attachment_id
        )", parameters);
        if (task.rowCount() == 0) return false;
        const std::string attachment[] = {std::string{task.value(0, 0)}, parameters[1], parameters[2]};
        const auto published = db.execute(R"(
            UPDATE app.attachment SET status = 'ready', size_bytes = $2::bigint, sha256 = decode($3, 'hex'),
                ready_at = clock_timestamp(), updated_at = clock_timestamp(),
                unreferenced_expires_at = clock_timestamp() + INTERVAL '1 day'
            WHERE id = $1::uuid AND status = 'uploading' RETURNING id
        )", attachment);
        if (published.rowCount() != 1) throw std::logic_error("Attachment is not in uploading state");
        return true;
    });
}

void FileRepository::failUpload(const boost::uuids::uuid& upload_id, const std::string& code) {
    const std::string parameters[] = {boost::uuids::to_string(upload_id), code};
    ConnectionLease lease{pool_};
    (void)lease.connection().execute(R"(
        WITH failed AS (
            UPDATE app.file_uploads SET state = 'failed', failure_code = $2,
                finished_at = clock_timestamp(), updated_at = clock_timestamp()
            WHERE id = $1::uuid AND state = 'receiving' RETURNING attachment_id
        )
        UPDATE app.attachment a SET status = 'failed', updated_at = clock_timestamp()
        FROM failed WHERE a.id = failed.attachment_id AND a.status = 'uploading'
    )", parameters);
}

VerifyPermissionResult FileRepository::verifyUploadPermission(const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& upload_id, const std::string& token_hash_hex, std::uint64_t content_length) {
    backend::logging::Scope operationLog{"FileRepository", "verifyUploadPermission"};
    const std::string parameters[] = {boost::uuids::to_string(user_id), boost::uuids::to_string(upload_id),
        token_hash_hex, std::to_string(content_length)};
    ConnectionLease lease{pool_};
    const auto result = lease.connection().execute(R"(
        SELECT (f.upload_token_hash = decode($3::text, 'hex')
                AND f.state = 'authorized' AND f.expires_at > clock_timestamp()
                AND u.is_active AND ud.status = 'active' AND a.status = 'uploading') AS permitted,
               (f.expected_size_bytes = $4::bigint) AS length_matches
        FROM app.file_uploads AS f
        JOIN app.users AS u ON u.id = f.uploader_id
        JOIN app.user_devices AS ud ON ud.device_id = f.uploader_device_id AND ud.user_id = f.uploader_id
        JOIN app.attachment AS a ON a.id = f.attachment_id
        WHERE f.uploader_id = $1::uuid AND f.id = $2::uuid
    )", parameters);
    if (result.rowCount() == 0) return VerifyPermissionResult::NotFound;
    if (result.value(0, 0) != "t") return VerifyPermissionResult::Unauthorized;
    if (result.value(0, 1) != "t") return VerifyPermissionResult::ContentLengthMismatch;
    return VerifyPermissionResult::Allowed;
}

std::variant<UploadPermissionError, UploadPermissionGranted> FileRepository::createFileUploadPermission(
    const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id,
    const std::string& token_hash_hex, const UploadPermissionRequest& request) {
    backend::logging::Scope operationLog{"FileRepository", "createFileUploadPermission"};
    if (request.client_request_id.is_nil() || request.expected_size_bytes <= 0) {
        return UploadPermissionError{"invalid_request", "A non-nil request ID and positive file size are required"};
    }
    if (token_hash_hex.size() != crypto_hash_sha256_BYTES * 2 ||
        token_hash_hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return UploadPermissionError{"invalid_upload_token_hash", "A 64-character hexadecimal token hash is required"};
    }

    const auto user = boost::uuids::to_string(user_id);
    const auto device = boost::uuids::to_string(device_id);
    const auto request_id = boost::uuids::to_string(request.client_request_id);
    const auto size = std::to_string(request.expected_size_bytes);
    char expected_sha256_hex[65]{};
    if (const auto& digest = request.expected_sha256) {
        sodium_bin2hex(expected_sha256_hex, sizeof expected_sha256_hex, digest->data(), digest->size());
    }

    ConnectionLease lease{pool_};
    auto& connection = lease.connection();
    return connection.withTransaction([&]() -> std::variant<UploadPermissionError, UploadPermissionGranted> {
        (void)connection.execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED");
        // 同设备的同一请求串行执行。等待锁后再读，才能看到先提交的任务。
        const std::string lock_parameters[] = {device, request_id};
        (void)connection.execute(R"(
            SELECT pg_advisory_xact_lock(hashtextextended(
                'file-upload-v1:' || $1::uuid::text || ':' || $2::uuid::text, 0))
        )", lock_parameters);

        const std::string owner_parameters[] = {user, device};
        const auto owner = connection.execute(R"(
            SELECT ud.device_id
            FROM app.user_devices AS ud
            JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.user_id = $1::uuid AND ud.device_id = $2::uuid
              AND ud.status = 'active' AND u.is_active = TRUE
            FOR SHARE OF ud, u
        )", owner_parameters);
        if (owner.rowCount() == 0) {
            return UploadPermissionError{"upload_permission_denied", "An active device belonging to the user is required"};
        }

        const std::string lookup_parameters[] = {user, device, request_id, size, expected_sha256_hex};
        const auto existing = connection.execute(R"(
            SELECT attachment_id, id, state,
                   to_char(expires_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
                   (expected_size_bytes = $4::bigint AND
                    expected_sha256 IS NOT DISTINCT FROM decode(NULLIF($5::text, ''), 'hex')),
                   (state = 'authorized' AND expires_at > clock_timestamp())
            FROM app.file_uploads
            WHERE uploader_id = $1::uuid AND uploader_device_id = $2::uuid AND client_request_id = $3::uuid
        )", lookup_parameters);
        if (existing.rowCount() != 0) {
            if (existing.value(0, 4) != "t") {
                return UploadPermissionError{"upload_request_conflict", "This request ID was already used with different file metadata"};
            }
            return UploadPermissionGranted{
                .attachment_id = boost::uuids::string_generator{}(std::string{existing.value(0, 0)}),
                .upload_id = boost::uuids::string_generator{}(std::string{existing.value(0, 1)}),
                .upload_state = std::string{existing.value(0, 2)},
                .expires_at = std::string{existing.value(0, 3)},
                .credentials = std::nullopt,
                .requires_renew = existing.value(0, 5) == "t"
            };
        }

        const auto attachment_id = boost::uuids::random_generator{}();
        const auto attachment = boost::uuids::to_string(attachment_id);
        const auto storage_key = "objects/attachments/" + attachment.substr(0, 2) + "/" +
            attachment.substr(2, 2) + "/" + attachment + ".bin";
        const std::string attachment_parameters[] = {attachment, user, device, storage_key};
        (void)connection.execute(R"(
            INSERT INTO app.attachment (id, uploader_id, uploader_device_id, storage_key)
            VALUES ($1::uuid, $2::uuid, $3::uuid, $4::text)
        )", attachment_parameters);

        const std::string upload_parameters[] = {attachment, user, device, request_id, size, expected_sha256_hex, token_hash_hex};
        const auto upload = connection.execute(R"(
            INSERT INTO app.file_uploads (
                attachment_id, uploader_id, uploader_device_id, client_request_id,
                expected_size_bytes, expected_sha256, upload_token_hash, expires_at
            )
            VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5::bigint,
                    decode(NULLIF($6::text, ''), 'hex'), decode($7::text, 'hex'),
                    clock_timestamp() + INTERVAL '10 minutes')
            RETURNING id, state,
                      to_char(expires_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
                      credential_version
        )", upload_parameters);
        return UploadPermissionGranted{
            .attachment_id = attachment_id,
            .upload_id = boost::uuids::string_generator{}(std::string{upload.value(0, 0)}),
            .upload_state = std::string{upload.value(0, 1)},
            .expires_at = std::string{upload.value(0, 2)},
            .credentials = UploadPermissionCredentials{
                .upload_url = "/upload_file",
                .upload_token = {},
                .credential_version = std::stoll(std::string{upload.value(0, 3)})
            }
        };
    });
}

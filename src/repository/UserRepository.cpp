#include "common/Logger.h"
//
// Created by yangb on 2026/9/2.
//

#include "repository/UserRepository.h"
#include "common/UniqueFd.h"
#include <string>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include "databases/DatabaseError.h"
//知道数据库结果，把数据库转换成领域里的对象
namespace {
    TimePoint parseRequestTime(std::string_view text) {
        std::int64_t microseconds = 0;
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), microseconds);
        if (error != std::errc{} || end != text.data() + text.size()) {
            throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid friend request timestamp");
        }
        return TimePoint{std::chrono::duration_cast<TimePoint::duration>(
            std::chrono::microseconds{microseconds})};
    }
    //这个命名空间是cpp内部的，只在这里使用
    boost::uuids::uuid parseId(std::string_view text)
    {
        // 将 UUID 文本解析为 boost::uuids::uuid。
        // 格式无效时，string_generator 会抛出异常。
        return boost::uuids::string_generator{}(
            text.begin(), text.end()
        );
    }

} // namespace

UserRepository::UserRepository(DatabasePool &databasePool): databasePool_(databasePool){}
std::optional<PasswordLoginRecord> UserRepository::FindLoginRecordByEmail(const std::string &email) {
    backend::logging::Scope operationLog{"UserRepository", "FindLoginRecordByEmail"};
    const std::string parameters[] = {email};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
        R"(
        SELECT
            u.id,
            u.display_name,
            u.is_active,
            p.password_hash,
            u.username
        FROM app.password_credentials AS p
        JOIN app.users AS u
            ON u.id = p.user_id
        WHERE lower(p.login_email) = lower($1)
    )",
        parameters
    );

    if (result.rowCount() == 0) {
        return std::nullopt;
    }
    return PasswordLoginRecord{
        .id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
        .is_active = (result.value(0, 2) == "t"),
        .password_hash = std::string{result.value(0, 3)},
        .username = std::string{result.value(0, 4)}
    };
}
std::optional<std::string> UserRepository::FindLoginRecordByUserId(const boost::uuids::uuid &user_id) {
    backend::logging::Scope operationLog{"UserRepository", "FindLoginRecordByUserId"};
    const std::string parameters[] = {boost::uuids::to_string(user_id)};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
        R"(
        SELECT
            p.password_hash
        FROM app.password_credentials AS p
        WHERE p.user_id = $1::uuid
    )",
        parameters
    );
    if (result.rowCount() == 0) {
        return std::nullopt;
    }
    return  std::string{result.value(0, 0)};
}

std::optional<User> UserRepository::findByEmail(const std::string& email)
{
    backend::logging::Scope operationLog{"UserRepository", "findByEmail"};
    //构造一个字符串数组
    const std::string parameters[] = {email};
    ConnectionLease lease(databasePool_);
    //表示第一个参数，就是email
    auto result = lease.connection().execute(
        R"(
            SELECT id, username, email
            FROM users
            WHERE email = $1
            LIMIT 1
        )",
        parameters
    );

    //如果没结果返回nullopt
    if (result.rowCount() == 0) {return std::nullopt;}

    return User{.id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
    };
}

std::optional<User> UserRepository::CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name,const std::string &username) {
    backend::logging::Scope operationLog{"UserRepository", "CreateNewUserByEmail"};
    ConnectionLease lease(databasePool_);

    return lease.connection().withTransaction([&]() -> User {
        //这是一个lambda函数，捕获this,不接受参数，返回User，他先把结果返回给withTransaction，然后再返回给service
        const std::string userParameters[] = {display_name,username};
        auto result = lease.connection().execute(
            R"(
                INSERT INTO app.users (display_name,username)
                VALUES ($1,$2)
                RETURNING id, display_name,username
            )",
            userParameters
        );
        const std::string userIdText{
            result.value(0, 0)
        };
        User user{
            .id = boost::uuids::string_generator{}(userIdText),
            .display_name = std::string{result.value(0, 1)},
            .username = std::string{result.value(0, 2)}
        };
        const std::string credentialParameters[] = {
            userIdText,
            email,
            password_hash
        };
        auto tempResult=lease.connection().execute(
            R"(
                INSERT INTO app.password_credentials (
                    user_id,
                    login_email,
                    password_hash
                )
                VALUES ($1, $2, $3)
            )",   credentialParameters
        );

        return user;
    });

}
std::optional<User> UserRepository::FindUserByUsername(const std::string &username) {
    backend::logging::Scope operationLog{"UserRepository", "FindUserByUsername"};
    const std::string Parameters[] = {username};
    ConnectionLease lease(databasePool_);
    //表示第一个参数，就是email
    auto result = lease.connection().execute(
        R"(
            SELECT id, display_name, username
            FROM users
            WHERE username = $1
            LIMIT 1
        )",
        Parameters
    );
    //如果没结果返回nullopt
    if (result.rowCount() == 0) {return std::nullopt;}
    return User{.id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
        .username = std::string{result.value(0, 2)},
    };

}
RevokeDeviceResult UserRepository::revokeUserDevice(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& device_id) {
    backend::logging::Scope operationLog{"UserRepository", "revokeUserDevice"};
    const std::string parameters[] = {boost::uuids::to_string(device_id)};
    const std::string actor = boost::uuids::to_string(my_id);
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> RevokeDeviceResult {
        // All dependent writes lock this row FOR SHARE and check active
        // in their own transaction. No table-wide or child-row locks needed.
        auto device = lease.connection().execute(R"(
            SELECT user_id, status
            FROM app.user_devices
            WHERE device_id = $1::uuid
            FOR UPDATE
        )", parameters);
        if (device.rowCount() == 0) return RevokeDeviceResult::DeviceNotFound;
        // Check ownership before exposing lifecycle state.
        if (device.value(0, 0) != actor) return RevokeDeviceResult::NotYourDevice;
        if (device.value(0, 1) == "revoked") return RevokeDeviceResult::AlreadyRevoked;
        if (device.value(0, 1) != "active") return RevokeDeviceResult::DeviceNotActive;
        auto updated = lease.connection().execute(R"(
            UPDATE app.user_devices
            SET status = 'revoked', revoked_at = CURRENT_TIMESTAMP
            WHERE device_id = $1::uuid AND status = 'active'
            RETURNING device_id
        )", parameters);
        if (updated.rowCount() != 1) {
            throw std::logic_error("Locked device was not revoked");
        }
        auto tokens = lease.connection().execute(R"(
            UPDATE app.refresh_tokens SET revoked_at = COALESCE(revoked_at, CURRENT_TIMESTAMP)
            WHERE device_id = $1::uuid
        )", parameters);
        // Preserve historical keys/envelopes. Device status controls access.
        return RevokeDeviceResult::Success;
    }); // Commit before success; database failures propagate.
}

FriendshipRequestRecord UserRepository::CreateNewFriendship(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id,const std::string &message) {
    backend::logging::Scope operationLog{"UserRepository", "CreateNewFriendship"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(friend_id),"pending",message};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
    R"(
        WITH inserted AS (
            INSERT INTO app.friend_request (
                requester_id,
                recipient_id,
                status,
                message
            )
            VALUES ($1::uuid, $2::uuid, $3::text, $4::text)
            RETURNING id, requester_id, message, created_at
        )
        SELECT
            inserted.id AS request_id,
            u.id,
            u.username,
            u.display_name,
            COALESCE(inserted.message, '') AS message,
            (EXTRACT(EPOCH FROM inserted.created_at) * 1000000)::bigint AS request_time
        FROM inserted
        JOIN app.users AS u ON u.id = inserted.requester_id
    )",
    Parameters
);

    if (result.rowCount() != 1) {
        throw std::runtime_error(
            "Insert friend request returned unexpected row count"
        );
    }

    return FriendshipRequestRecord{
        .request_id = boost::uuids::string_generator{}(
            std::string{result.value(0, 0)}
        ),
        .id = boost::uuids::string_generator{}(
            std::string{result.value(0, 1)}
        ),
        .username = std::string{result.value(0, 2)},
        .display_name = std::string{result.value(0, 3)},
        .message = std::string{result.value(0, 4)},
        .request_time = parseRequestTime(result.value(0, 5))
    };

}

std::optional<std::vector<FriendshipRequestRecord>> UserRepository::FetchUnprocessedRequest(const boost::uuids::uuid &my_id) {
    backend::logging::Scope operationLog{"UserRepository", "FetchUnprocessedRequest"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),"pending"};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
        SELECT
            fr.id AS request_id,
            u.id AS requester_id,
            u.username,
            u.display_name,
            fr.message,
            (EXTRACT(EPOCH FROM fr.created_at) * 1000000)::bigint AS request_time
        FROM app.friend_request AS fr
        JOIN app.users AS u
            ON u.id = fr.requester_id
        WHERE fr.recipient_id = $1
          AND fr.status = $2
        ORDER BY fr.created_at DESC, fr.id DESC
    )",
        Parameters
    );
    std::vector<FriendshipRequestRecord> request_summary;

    if (result.rowCount() == 0) {return std::nullopt;}
    request_summary.reserve(result.rowCount());

    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        request_summary.push_back(FriendshipRequestRecord{
            .request_id = boost::uuids::string_generator{}(std::string{result.value(i, 0)}),
            .id = boost::uuids::string_generator{}(std::string{result.value(i, 1)}),
            .username = std::string{result.value(i, 2)},
            .display_name = std::string{result.value(i, 3)},
            .message = result.isNull(i, 4)? std::string{}: std::string{result.value(i, 4)},
            .request_time = parseRequestTime(result.value(i, 5))
        });
    }
    return request_summary;
}
std::vector<User> UserRepository::FetchFriends(const boost::uuids::uuid& my_id) {
    backend::logging::Scope operationLog{"UserRepository", "FetchFriends"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
            SELECT u.id, u.display_name, u.username
            FROM app.friend_relation AS fr
            JOIN app.users AS u ON u.id = fr.friend_id
            WHERE fr.user_id = $1
            ORDER BY u.username, u.id
        )",
        Parameters
    );
    std::vector<User> friends;
    friends.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        friends.push_back(User{
            .id = parseId(result.value(i, 0)),
            .display_name = std::string{result.value(i, 1)},
            .username = std::string{result.value(i, 2)}
        });
    }
    return friends;
}

FriendRequestProcessResult UserRepository::processFriendshipRequest(
    const boost::uuids::uuid& my_uuid, const boost::uuids::uuid& request_id,
    FriendRequestAction action) {
    backend::logging::Scope operationLog{"UserRepository", "processFriendshipRequest"};
    std::string targetStatus;
    switch (action) {
        case FriendRequestAction::Accept: targetStatus = "accepted"; break;
        case FriendRequestAction::Reject: targetStatus = "rejected"; break;
        case FriendRequestAction::Cancel: targetStatus = "cancelled"; break;
        default: return FriendRequestProcessResult::InvalidAction;
    }
    const std::string lookup[] = {boost::uuids::to_string(request_id)};
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> FriendRequestProcessResult {
        // Serialize accept/reject/cancel for the same request.
        auto record = lease.connection().execute(R"(
            SELECT requester_id, recipient_id, status
            FROM app.friend_request WHERE id = $1 FOR UPDATE
        )", lookup);
        if (record.rowCount() == 0) return FriendRequestProcessResult::NotFound;

        const auto actor = boost::uuids::to_string(my_uuid);
        const std::string requester{record.value(0, 0)};
        const std::string recipient{record.value(0, 1)};
        const std::string status{record.value(0, 2)};
        const auto& authorizedActor = action == FriendRequestAction::Cancel
            ? requester : recipient;
        if (actor != authorizedActor) return FriendRequestProcessResult::Forbidden;
        if (status == targetStatus) return FriendRequestProcessResult::AlreadyProcessed;
        if (status != "pending") return FriendRequestProcessResult::Conflict;

        const std::string update[] = {lookup[0], targetStatus};
        auto updated = lease.connection().execute(R"(
            UPDATE app.friend_request
            SET status = $2, processed_at = CURRENT_TIMESTAMP
            WHERE id = $1 AND status = 'pending'
            RETURNING id
        )",    update);
        if (updated.rowCount() != 1) return FriendRequestProcessResult::Conflict;

        if (action == FriendRequestAction::Accept) {
            const std::string members[] = {requester, recipient};
            auto inserted = lease.connection().execute(R"(
                INSERT INTO app.friend_relation (user_id, friend_id)
                VALUES ($1, $2), ($2, $1)
                ON CONFLICT (user_id, friend_id) DO NOTHING
            )",   members);
        }
        return FriendRequestProcessResult::Success;
    });
}
bool UserRepository::VerifyFriendShip(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id) {
    backend::logging::Scope operationLog{"UserRepository", "VerifyFriendShip"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(friend_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
       R"(
            SELECT fr.id
            FROM app.friend_relation AS fr
            WHERE fr.user_id = $1 and fr.friend_id = $2
        )",
       Parameters
   );
    if (result.rowCount() == 0) return false;
    return true;
}

bool UserRepository::VerifyUserDeviceRelation(const boost::uuids::uuid &my_id,const boost::uuids::uuid &device_id) {
    backend::logging::Scope operationLog{"UserRepository", "VerifyUserDeviceRelation"};
    const std::string parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(device_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
            SELECT
                CASE
                    WHEN ud.user_id = $1::uuid THEN 'true'
                    ELSE 'false'
                END AS belongs_to_user,
                CASE
                    WHEN ud.status = 'active'
                         AND u.is_active = TRUE THEN 'true'
                    ELSE 'false'
                END AS is_active
            FROM app.user_devices AS ud
            LEFT JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.device_id = $2::uuid
        )",
        parameters
    );

    if (result.rowCount() == 0) {
        throw DeviceRelationException{
            DeviceRelationError::NoSuchDevice
        };
    }

    // 先验证归属，再检查设备和账号状态。
    if (result.value(0, 0) != "true") {
        throw DeviceRelationException{
            DeviceRelationError::NotYourDevice
        };
    }

    if (result.value(0, 1) != "true") {
        throw DeviceRelationException{
            DeviceRelationError::NotActivated
        };
    }

    return true;
}

std::optional<boost::uuids::uuid> UserRepository::saveAvatar(const boost::uuids::uuid& my_id, std::string_view avatar_bytes) {
    backend::logging::Scope operationLog{"UserRepository", "saveAvatar"};
    const auto avatar_id = boost::uuids::random_generator{}();
    //生成随机的uuid,文件名为uuid.jpg,如a7c2d8e1-4f56-47b9-8a31-2d6e390bc123.jpg，
    const auto id = boost::uuids::to_string(avatar_id);
    //相对目录是object/avatars/a7/c2/a7c2d8e1-4f56-47b9-8a31-2d6e390bc123.jpg
    const std::string storage_key = "objects/avatars/" + id.substr(0, 2) + "/" + id.substr(2, 2) + "/" + id + ".jpg";
    //获取根目录
    const char* configured_root = std::getenv("FILE_STORAGE_ROOT");
    auto root = std::filesystem::path{configured_root && *configured_root ? configured_root : "/srv/chat-files"}.lexically_normal();
    if (root.has_relative_path() && !root.has_filename()) root = root.parent_path();
    if (!root.is_absolute() || !std::filesystem::is_directory(root)) {
        throw std::system_error(std::make_error_code(std::errc::not_a_directory), "FILE_STORAGE_ROOT must be an existing absolute directory");
    }
    const auto staging_dir = root / "staging" / "avatars";
    //这是临时路径
    const auto staging_path = staging_dir / (id + ".part");
    const auto final_path = root / storage_key;
    const std::string user_parameters[] = {boost::uuids::to_string(my_id)};
    const std::string avatar_parameters[] = {id, user_parameters[0], storage_key, std::to_string(avatar_bytes.size())};

    const auto sync_fd = [](int fd) {
        int result;
        do { result = ::fsync(fd); } while (result == -1 && errno == EINTR);
        if (result == -1) throw std::system_error(errno, std::generic_category(), "sync avatar storage");
    };

    bool staging_created = false;
    bool final_published = false;
    bool ready_to_commit = false;
    try {
        ConnectionLease lease{databasePool_};
        auto& connection = lease.connection();
        return connection.withTransaction([&]() -> std::optional<boost::uuids::uuid> {
            // 锁定用户行，首次上传和后续替换都按同一把锁串行执行。
            const auto user = connection.execute(
                "SELECT id FROM app.users WHERE id = $1::uuid AND is_active = TRUE FOR UPDATE", user_parameters);
            if (user.rowCount() == 0) return std::nullopt;
            const auto previous = connection.execute(
                "SELECT avatar_id FROM app.user_avatars WHERE user_id = $1::uuid", user_parameters);

            std::filesystem::create_directories(staging_dir);
            std::filesystem::create_directories(final_path.parent_path());
            {
                //创建fd,open的参数表示只写，离开作用域的时候会关闭
                //如果路径已经存在就失败
                int file_fd;
                do {
                    file_fd = ::open(staging_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
                } while (file_fd == -1 && errno == EINTR);
                if (file_fd == -1) throw std::system_error(errno, std::generic_category(), "create temporary avatar");
                UniqueFd file{file_fd};
                staging_created = true;

                // 同步写入已经通过 Service 校验的 JPEG，write 可能只写入一部分。
                //remaining保证写完
                auto remaining = avatar_bytes;
                while (!remaining.empty()) {
                    const auto written = ::write(file.get(), remaining.data(), remaining.size());
                    if (written == -1) {
                        const int error = errno;
                        if (error == EINTR) continue;
                        throw std::system_error(error, std::generic_category(), "write avatar");
                    }
                    if (written == 0) {
                        throw std::system_error(std::make_error_code(std::errc::io_error), "avatar write made no progress");
                    }
                    remaining.remove_prefix(static_cast<std::size_t>(written));
                }

                struct stat file_info{};
                //fstat从fd读取文件大小，保证st_size等于原始大小
                if (::fstat(file.get(), &file_info) == -1) {
                    throw std::system_error(errno, std::generic_category(), "check temporary avatar size");
                }
                if (file_info.st_size < 0 || static_cast<std::uintmax_t>(file_info.st_size) != avatar_bytes.size()) {
                    throw std::system_error(std::make_error_code(std::errc::io_error), "temporary avatar size does not match JPEG size");
                }
                //	要求内核把文件内容及相关元数据同步到底层存储，并检查同步结果。
                sync_fd(file.get());
                // 在事务内检查关闭结果。release 避免 UniqueFd 析构时再次关闭文件 fd。
                //保证关闭
                if (::close(file.release()) == -1) {
                    throw std::system_error(errno, std::generic_category(), "close temporary avatar");
                }
            }
            //绑定新记录给头像，并把旧的记录标记为revoke
            (void)connection.execute(R"(
                INSERT INTO app.avatars (id, uploader_id, storage_key, size_bytes, created_at)
                VALUES ($1::uuid, $2::uuid, $3, $4::bigint, clock_timestamp())
            )",   avatar_parameters);
            const std::string binding_parameters[] = {user_parameters[0], id};
            (void)connection.execute(R"(
                INSERT INTO app.user_avatars (user_id, avatar_id, updated_at)
                VALUES ($1::uuid, $2::uuid, clock_timestamp())
                ON CONFLICT (user_id) DO UPDATE
                SET avatar_id = EXCLUDED.avatar_id, updated_at = EXCLUDED.updated_at
            )",   binding_parameters);
            if (previous.rowCount() != 0) {
                const std::string previous_parameters[] = {std::string{previous.value(0, 0)}, user_parameters[0]};
                (void)connection.execute(R"(
                    UPDATE app.avatars
                    SET status = 'revoked', revoked_at = GREATEST(clock_timestamp(), created_at)
                    WHERE id = $1::uuid AND uploader_id = $2::uuid AND status = 'active'
                )",
                  previous_parameters);
            }

            // 正式路径和临时路径位于同一文件系统。link 以不覆盖方式发布文件。
            if (::link(staging_path.c_str(), final_path.c_str()) == -1) {
                throw std::system_error(errno, std::generic_category(), "publish avatar");
            }
            final_published = true;
            std::filesystem::remove(staging_path);
            staging_created = false;

            // 同步正式目录及其父目录，使新建的分片目录也在提交前持久化。
            for (auto directory = final_path.parent_path(); ; directory = directory.parent_path()) {
                UniqueFd directory_fd{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
                if (!directory_fd.valid()) throw std::system_error(errno, std::generic_category(), "open avatar directory");
                sync_fd(directory_fd.get());
                if (directory == root || directory == directory.root_path()) break;
            }
            ready_to_commit = true;
            return avatar_id;
        });
    } catch (...) {
        // COMMIT 回包丢失时结果可能已提交，保留正式文件供后续核对。
        std::error_code error;
        if (staging_created) std::filesystem::remove(staging_path, error);
        if (error) backend::logging::Logger::write(backend::logging::Level::Warn, "UserRepository", "avatar_staging_cleanup_failed");
        if (final_published && !ready_to_commit) {
            std::filesystem::remove(final_path, error);
            if (error) backend::logging::Logger::write(backend::logging::Level::Warn, "UserRepository", "avatar_cleanup_failed");
        }
        if (ready_to_commit) backend::logging::Logger::write(backend::logging::Level::Warn, "UserRepository", "avatar_commit_failed_file_retained", {{"avatar_id", std::string_view{id}}});
        throw;
    }
}

//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERREPOSITORY_H
#define CPP_BACKEND_SERVER_USERREPOSITORY_H
#include <boost/uuid/uuid.hpp>

#include <optional>
#include <string>
#include <vector>
#include <exception>

#include "RefreshTokenRepository.h"
#include "databases/DatabaseConnection.h"
#include "boost/uuid/uuid_io.hpp"
#include "databases/DatabaseError.h"
#include "domain/User.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabasePool.h"
#include "domain/FriendshipRequestRecord.h"
#include "domain/FriendRequestProcess.h"
//负责接受service来的业务数据，包装成数据库需要形式，执行数据库连接和命令，返回service层处理需要的结果
//借用但是不拥有数据库连接，不负责析构
struct PasswordLoginRecord {
    boost::uuids::uuid id;
    std::string display_name;
    bool is_active;
    std::string password_hash;
    std::string username;
};

class UserRepository {
public:
    explicit UserRepository(DatabasePool &databasePool);
    std::optional<User> findByEmail(const std::string& email);
    std::optional<User> CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name,const std::string &username);
    std::optional<PasswordLoginRecord> FindLoginRecordByEmail(const std::string &email);
    void InsertRefreshToken(const boost::uuids::uuid &id,std::string &refreshToken_hash);
    std::optional<User> FindUserByUsername(const std::string &username);
    void CreateNewFriendship(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id,const std::string &message);
    std::optional<std::vector<FriendshipRequestRecord>> FetchUnprocessedRequest(const boost::uuids::uuid &my_id);
    std::vector<User> FetchFriends(const boost::uuids::uuid& my_id);
    FriendRequestProcessResult processFriendshipRequest(const boost::uuids::uuid& my_id,
        const boost::uuids::uuid& request_id, FriendRequestAction action);
    bool VerifyFriendShip(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id);

private:
    DatabasePool &databasePool_;
};

#endif //CPP_BACKEND_SERVER_USERREPOSITORY_H

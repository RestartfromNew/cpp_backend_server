//
// Created by yangb on 2026/9/2.
//

#include "service/UserService.h"
UserService::UserService(
    UserRepository& repository
)
    : repository_(repository)
{
}

std::optional<User>
UserService::findUserByEmail(const std::string& email)
{
    return repository_.findByEmail(email);
    //后面可能加上其它业务逻辑
}

std::optional<User> UserService::findUserByUserName(const std::string& username) {
    auto result=repository_.FindUserByUsername(username);
    return result;
}
bool UserService::requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string& message)
{
    try {
        repository_.CreateNewFriendship(my_uuid,friend_uuid,message);
        return true;
    }
    catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Query &&
            error.code() == "23505" &&
            error.constraint() == "friend_request_pending_pair_unique") {
            return false;
            } else {
                throw; // 其他数据库错误继续交给上层处理。
            }
    }

}
std::optional<std::vector<FriendshipRequestRecord>> UserService::fetchFriendshipRequest(const boost::uuids::uuid& my_uuid) {
    auto result=repository_.FetchUnprocessedRequest(my_uuid);
    return result;

}
std::vector<User> UserService::fetchFriends(const boost::uuids::uuid& my_uuid) {
    return repository_.FetchFriends(my_uuid);
}

FriendRequestProcessResult UserService::processFriendshipRequest(
    const boost::uuids::uuid& my_uuid, const boost::uuids::uuid& request_id,
    const std::string& process) {
    FriendRequestAction action;
    if (process == "accepted") action = FriendRequestAction::Accept;
    else if (process == "rejected") action = FriendRequestAction::Reject;
    else if (process == "cancelled") action = FriendRequestAction::Cancel;
    else return FriendRequestProcessResult::InvalidAction;

    // Database failures must propagate; never silently report success.
    return repository_.processFriendshipRequest(my_uuid, request_id, action);
}

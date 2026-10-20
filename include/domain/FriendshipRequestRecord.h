//
// Created by yangb on 2026/10/2.
//

#ifndef CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H
#define CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H
/** @brief 好友请求及对方展示资料的返回记录；message 可为空，时间字段使用统一 TimePoint 表示。 */
struct FriendshipRequestRecord {
    boost::uuids::uuid request_id;
    boost::uuids::uuid id;
    std::string username;
    std::string display_name;
    std::string message;
    TimePoint request_time;

};
#endif //CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H

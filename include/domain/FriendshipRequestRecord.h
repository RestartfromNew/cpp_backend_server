//
// Created by yangb on 2026/10/2.
//

#ifndef CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H
#define CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H
struct FriendshipRequestRecord {
    boost::uuids::uuid request_id;
    boost::uuids::uuid id;
    std::string username;
    std::string display_name;
    std::string message;
    TimePoint request_time;

};
#endif //CPP_BACKEND_SERVER_FRIENDSHIPREQUESTRECORD_H

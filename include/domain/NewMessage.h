#ifndef CPP_BACKEND_SERVER_NEWMESSAGE_H
#define CPP_BACKEND_SERVER_NEWMESSAGE_H
#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <string>
#include <vector>
struct NewTextMessageRequest {
    boost::uuids::uuid message_id;
    boost::uuids::uuid recipient_device_id;
    std::string protocol_version;
    std::vector<std::uint8_t> packet;
};

#endif

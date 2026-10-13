//
// Created by yangb on 2026/10/13.
//

#ifndef CPP_BACKEND_SERVER_ENVELOPE_H
#define CPP_BACKEND_SERVER_ENVELOPE_H
#include <boost/uuid/uuid.hpp>
#include <vector>
struct Envelope {
    boost::uuids::uuid envelope_id;
    boost::uuids::uuid message_id;
    boost::uuids::uuid sender_device_id;
    boost::uuids::uuid recipient_device_id;
    std::string protocol_version;
    std::vector<std::uint8_t> packet;
    std::int64_t seq;

};
struct EnvelopePage {
    std::vector<Envelope> envelopes;
    bool has_more = false;
};
#endif //CPP_BACKEND_SERVER_ENVELOPE_H

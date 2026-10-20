//
// Created by yangb on 2026/10/13.
//

#ifndef CPP_BACKEND_SERVER_ENVELOPE_H
#define CPP_BACKEND_SERVER_ENVELOPE_H
#include <boost/uuid/uuid.hpp>
#include <vector>
#include <string>
#include <cstdint>
/** @brief 已存储信封：message_id 用于客户端重试，envelope_id 是记录 ID，seq 是无向设备对内序号，packet 为不透明字节。 */
struct Envelope {
    boost::uuids::uuid envelope_id;
    boost::uuids::uuid message_id;
    boost::uuids::uuid sender_device_id;
    boost::uuids::uuid recipient_device_id;
    std::string protocol_version;
    std::vector<std::uint8_t> packet;
    std::int64_t seq;

};
/** @brief 同步分页结果；空列表是合法成功，has_more 指示继续按游标拉取，不能当作身份拒绝。 */
struct EnvelopePage {
    std::vector<Envelope> envelopes;
    bool has_more = false;
};
#endif //CPP_BACKEND_SERVER_ENVELOPE_H

#ifndef CPP_BACKEND_SERVER_NEWMESSAGE_H
#define CPP_BACKEND_SERVER_NEWMESSAGE_H
#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <string>
#include <vector>
/** @brief 发送信封的业务输入；packet 已由客户端加密/编码，重试必须保留相同 message_id 和字节。 */
struct NewTextMessageRequest {
    boost::uuids::uuid message_id;
    boost::uuids::uuid recipient_device_id;
    std::string protocol_version;
    std::vector<std::uint8_t> packet;
};

#endif

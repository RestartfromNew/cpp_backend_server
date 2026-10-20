#pragma once
#include <boost/uuid/uuid.hpp>
#include <vector>
/** @brief 本机设备及待确认信封 ID；只确认本设备收到的记录，ACK 不代表解密成功或已读。 */
struct AckEnvelopeRequest {
    boost::uuids::uuid device_id;
    std::vector<boost::uuids::uuid> envelope_ids;
};

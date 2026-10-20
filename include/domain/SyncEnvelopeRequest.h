//
// Created by yangb on 2026/10/13.
//

#ifndef CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H
#define CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H
#include <boost/uuid/uuid.hpp>
#include <vector>
/** @brief 某个 peer_device_id 的已持久化 seq 水位；after_seq=0 包含首次同步，不能仅按最大在线推送序号推进。 */
struct EnvelopeSyncCursor {
    boost::uuids::uuid peer_device_id;
    std::int64_t after_seq;
};
/** @brief 同步本机设备及各对端游标；未提供游标的发送设备由数据库按初始水位发现。 */
struct SyncEnvelopeRequest {
    boost::uuids::uuid device_id;
    std::vector<EnvelopeSyncCursor> cursors;
};
#endif //CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H

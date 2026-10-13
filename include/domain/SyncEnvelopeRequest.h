//
// Created by yangb on 2026/10/13.
//

#ifndef CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H
#define CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H
#include <boost/uuid/uuid.hpp>
#include <vector>
struct EnvelopeSyncCursor {
    boost::uuids::uuid peer_device_id;
    std::int64_t after_seq;
};
struct SyncEnvelopeRequest {
    boost::uuids::uuid device_id;
    std::vector<EnvelopeSyncCursor> cursors;
};
#endif //CPP_BACKEND_SERVER_SYNCENVELOPEREQUEST_H

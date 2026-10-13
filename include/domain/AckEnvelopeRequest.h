#pragma once
#include <boost/uuid/uuid.hpp>
#include <vector>
struct AckEnvelopeRequest {
    boost::uuids::uuid device_id;
    std::vector<boost::uuids::uuid> envelope_ids;
};

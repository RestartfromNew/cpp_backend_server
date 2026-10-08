#ifndef CPP_BACKEND_SERVER_ONETIMEPREKEY_H
#define CPP_BACKEND_SERVER_ONETIMEPREKEY_H

#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <optional>
#include <string>

struct OneTimePrekey {
    boost::uuids::uuid device_id;
    std::int64_t prekey_id;
    std::string public_key_hex;
};

enum class PrekeyClaimStatus { Success, NotFriends, DeviceUnavailable, NoPrekey };
struct PrekeyClaimResult {
    PrekeyClaimStatus status;
    std::optional<OneTimePrekey> prekey = std::nullopt;
};

#endif

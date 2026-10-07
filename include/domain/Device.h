//
// Created by yangb on 2026/10/7.
//

#ifndef CPP_BACKEND_SERVER_DEVICE_H
#define CPP_BACKEND_SERVER_DEVICE_H
#include <string>
#include <boost/uuid/uuid.hpp>
#include <vector>

#include <chrono>
using TimePoint = std::chrono::system_clock::time_point;
enum DeviceStatus {
    active,revoked,pending
};
struct Device {
    boost::uuids::uuid device_id;
    boost::uuids::uuid user_id;
    std::string device_name;
    DeviceStatus status;
    std::vector<std::uint8_t> identity_public_key;
    std::string protocol_suite;

};
#endif //CPP_BACKEND_SERVER_DEVICE_H

//
// Created by yangb on 2026/10/7.
//

#ifndef CPP_BACKEND_SERVER_DEVICEKEYBUNDLE_H
#define CPP_BACKEND_SERVER_DEVICEKEYBUNDLE_H
#include <boost/uuid/uuid.hpp>
#include <vector>
/** @brief 设备公钥包；服务端存储和分发，客户端负责密码学验证及建立加密会话。 */
struct DeviceKeyBundle {
    boost::uuids::uuid device_id;
    int signed_preKey_id;
    std::vector<std::uint8_t> preKey_public;
    std::vector<std::uint8_t> preKey_signature;
    bool is_current;

};

#endif //CPP_BACKEND_SERVER_DEVICEKEYBUNDLE_H

//
// Created by yangb on 2026/10/14.
//

#ifndef CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H
#define CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H
#include "repository/UserRepository.h"
// 会查库的 Session 身份结果；非 Success 不是 JWT 签名错误，基础设施异常不会吞成普通拒绝。
enum IdentityState {
    Success,NoSuchDevice,NotActivated,NotYourDevice
};

/** @brief 把用户/设备归属与 active 检查映射为 IdentityState；此调用会查库，不是纯内存 JWT 验证。 */
class SessionIdentityService {
    public:
    SessionIdentityService(UserRepository &userRepository);
    IdentityState verifySessionIdentity(const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id);
private:
    UserRepository &user_repository_;

};


#endif //CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H

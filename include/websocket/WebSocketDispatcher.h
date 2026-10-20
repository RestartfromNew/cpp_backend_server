//
// Created by yangb on 2026/9/30.
//

#ifndef CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H
#define CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <nlohmann/json.hpp>
#include <http/AuthMiddleWare.h>
#include <optional>
#include <cstdint>
#include <vector>
#include <service/ChatService.h>
#include <domain/SyncEnvelopeRequest.h>
#include "service/SessionIdentityService.h"
class ServiceThreadPool;


/** @brief 边界解析后的发送设备与业务请求；格式正确不代表拥有发送权限，事务内仍要校验。 */
struct ParsedNewMessage {
    boost::uuids::uuid sender_device_id;
    NewTextMessageRequest request;
};

/** @brief 业务 JSON 路由/格式及 Session 绑定检查，构造 Task 和 Reply；没有 Session map 所有权，保持 switch 路由结构。 */
class WebSocketDispatcher {
    public:
    using Task=std::function<void()>;
    using Reply = std::function<void(nlohmann::json)>;
    //这个东西负责协调两个池
    explicit WebSocketDispatcher(ServiceThreadPool &threadPool,AuthMiddleWare &AuthMiddleWare,SessionIdentityService &sessionIdentityService);
    void dispatch(const boost::uuids::uuid &session_device_id,const boost::uuids::uuid &session_user_id,nlohmann::json frame,Reply reply);
private:
    std::optional<ParsedNewMessage> parseNewMessage(const nlohmann::json& payload,const Reply& reply) const;
    std::optional<SyncEnvelopeRequest> parseSyncEnvelope(
        const nlohmann::json& payload, const Reply& reply) const;
    std::optional<AckEnvelopeRequest> parseAckEnvelope(
        const nlohmann::json& payload, const Reply& reply) const;
    bool validateSessionRequest(const AuthResult& authResult,const boost::uuids::uuid& session_user_id,
    const boost::uuids::uuid& session_device_id,const nlohmann::json& payload,const Reply& reply) const;


    ServiceThreadPool &threadPool_;
    AuthMiddleWare &AuthMiddleWare_;
    SessionIdentityService &sessionIdentityService_;

};


#endif //CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H

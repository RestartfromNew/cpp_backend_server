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


struct ParsedNewMessage {
    boost::uuids::uuid sender_device_id;
    NewTextMessageRequest request;
};

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
    ServiceThreadPool &threadPool_;
    AuthMiddleWare &AuthMiddleWare_;
    SessionIdentityService &sessionIdentityService_;

};


#endif //CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H

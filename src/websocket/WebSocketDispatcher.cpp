//
// Created by yangb on 2026/9/30.
//

#include "websocket/WebSocketDispatcher.h"

#include <sodium/utils.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <stdexcept>

#include "service/ServiceThreadPool.h"
namespace {

const std::string& requireString(
    const nlohmann::json& body,
    const char* field
) {
    const auto value = body.find(field);

    if (value == body.end() || !value->is_string() ||
        value->get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(
            std::string{field} + " must be a non-empty string"
        );
    }

    return value->get_ref<const std::string&>();
}

boost::uuids::uuid requireUuid(
    const nlohmann::json& body,
    const char* field
) {
    const auto& text = requireString(body, field);

    if (text.size() != 36 ||
        text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') {
        throw std::invalid_argument(
            std::string{field} + " must be a UUID"
        );
    }

    try {
        return boost::uuids::string_generator{}(text);
    } catch (const std::exception&) {
        throw std::invalid_argument(
            std::string{field} + " must be a UUID"
        );
    }
}

std::vector<std::uint8_t> requirePacket(
    const nlohmann::json& body
) {
    const auto& encoded = requireString(body, "packet_base64");

    constexpr std::size_t maxPacketBytes = 64 * 1024;
    constexpr std::size_t maxEncodedBytes =
        4 * ((maxPacketBytes + 2) / 3);

    if (encoded.size() > maxEncodedBytes ||
        encoded.size() % 4 != 0) {
        throw std::invalid_argument(
            "packet_base64 is invalid or exceeds 64 KiB"
        );
    }

    std::vector<std::uint8_t> packet(maxPacketBytes);
    std::size_t decodedLength = 0;
    const char* end = nullptr;

    const int result = sodium_base642bin(
        packet.data(),
        packet.size(),
        encoded.data(),
        encoded.size(),
        nullptr,             // 不忽略空格等字符
        &decodedLength,
        &end,
        sodium_base64_VARIANT_ORIGINAL
    );

    if (result != 0 ||
        end != encoded.data() + encoded.size() ||
        decodedLength == 0) {
        throw std::invalid_argument(
            "packet_base64 must contain valid non-empty Base64"
        );
    }

    packet.resize(decodedLength);
    return packet;
}

}
enum class WebsocketRouter {
    //发一条新消息
    NewTextMessageTest,
    NewTextMessage,
    UnKnown

};
WebsocketRouter parseType(const std::string& type) {
    if (type == "chat.newTextMessage_Test")
        return WebsocketRouter::NewTextMessageTest;
    if (type == "chat.newTextMessage")
        return WebsocketRouter::NewTextMessage;
    return WebsocketRouter::UnKnown;
}
WebSocketDispatcher::WebSocketDispatcher(ServiceThreadPool &threadPool,AuthMiddleWare &AuthMiddleWare):threadPool_(threadPool),AuthMiddleWare_(AuthMiddleWare) {};
void WebSocketDispatcher::dispatch(nlohmann::json frame,Reply reply) {

    nlohmann::json payload=std::move(frame["payload"]);
    if (!payload.is_object()) {
        reply(nlohmann::json{
            {"type", "error"},
            {"error", {{"code", "invalid_payload"}}}
        });
        return;
    }

    auto authorization = payload.find("authorization");
    if (authorization == payload.end()) {
        authorization=payload.find("Authorization");
    }
    if (authorization == payload.end() ||
        !authorization->is_string()) {
        reply(nlohmann::json{
            {"type", "error"},
            {"error", {{"code", "missing_authorization"}}}
        });
        return;
        }
    const auto authResult=AuthMiddleWare_.authenticate(authorization->get_ref<const std::string&>());
    // 认证失败：保存的是 AuthError
    if (std::holds_alternative<AuthError>(authResult.result)) {
        const auto error = std::get<AuthError>(authResult.result);
        reply(nlohmann::json{
            {"type", "error"},
            {"error", {
                {"code", error == AuthError::Expired
                    ? "access_token_expired"
                    : "invalid_access_token"}
            }}
        });
        return;
    }
    // 认证成功：保存的是用户 UUID
    const auto userId =std::get<boost::uuids::uuid>(authResult.result);
    nlohmann::json header=frame["header"];
    std::string payload_format= frame["payload_format"];
    // std::string device_id=payload["device_id"];
    // std::string receiver_device_id=payload["receiver_device_id"];
    std::string type=payload["type"];
    switch (parseType(type)) {
        case WebsocketRouter::NewTextMessageTest: {
            //这里task是整个函数体，并没有执行函数
            Task task=[this,payload=std::move(payload),header=std::move(header),reply=std::move(reply)]()mutable  {
                const std::string id=payload["id"];
                const std::string message=payload["message"];
                threadPool_.chatService.printId(id,message);
                nlohmann::json response{
                    {"type", "newTextMessage.result"},
                    {"success", true},
                    {"id", id}
                };
                //reply是一个需要json作为参数的函数，它定义在worker的dispatch中。
                reply(std::move(response));
            };
            //这里将Task交给线程池
            threadPool_.receiveTask(std::move(task));
            return;
        }
        case WebsocketRouter::NewTextMessage: {
            Task task=[this,payload=std::move(payload),header=std::move(header),userId,reply]()mutable {
                const auto parsed = parseNewMessage(payload, reply);
                if (!parsed) {
                    return; // 解析函数已经回复错误
                }
                const auto& senderDeviceId = parsed->sender_device_id;
                const auto& request = parsed->request;
                nlohmann::json response{
                    {"type", "newTextMessage.result"},
                    {"success", false}
                };
                const auto setError = [&](const char* code, const char* message) {
                    response["error"] = {{"code", code}, {"message", message}};
                };
                try {
                    const auto result = threadPool_.chatService.newTextMessage(
                        userId, senderDeviceId, request);
                    switch (result.status) {
                        case NewTextMessageStatus::Success:
                        case NewTextMessageStatus::Duplicate_request:
                            if (!result.envelope) {
                                setError("internal_server_error", "Missing storage result");
                                break;
                            }
                            response["success"] = true;
                            response["duplicate"] = result.status == NewTextMessageStatus::Duplicate_request;
                            response["message_id"] = boost::uuids::to_string(result.envelope->message_id);
                            response["envelope_id"] = boost::uuids::to_string(result.envelope->envelope_id);
                            response["seq"] = result.envelope->seq;
                            break;
                        case NewTextMessageStatus::InvalidRequest:
                            setError("invalid_request", "Invalid message fields");
                            break;
                        case NewTextMessageStatus::PermissionDenied:
                            setError("send_permission_denied", "Sender or recipient is not permitted");
                            break;
                        case NewTextMessageStatus::Conflict:
                            setError("envelope_conflict", "Message identifier already exists with different content");
                            break;
                        case NewTextMessageStatus::DatabaseUnavailable:
                            setError("database_unavailable", "Retry later with the same message identifier and packet");
                            break;
                        case NewTextMessageStatus::RetryLater:
                            setError("retry_later", "Retry later with the same message identifier and packet");
                            break;
                        case NewTextMessageStatus::InternalError:
                        default:
                            setError("internal_server_error", "Unable to save message");
                            break;
                    }
                } catch (const std::exception&) {
                    response = {
                        {"type", "newTextMessage.result"}, {"success", false},
                        {"error", {{"code", "internal_server_error"},
                                   {"message", "Unable to process message"}}}
                    };
                }
                // 与测试路由相同：构造响应，再交回原 Worker。
                reply(std::move(response));
            };
            try {
                threadPool_.receiveTask(std::move(task));
            } catch (const std::exception&) {
                reply(nlohmann::json{
                    {"type", "newTextMessage.result"}, {"success", false},
                    {"error", {{"code", "server_busy"},
                               {"message", "Service task was not queued"}}}
                });
            }
            return;
        }
        case WebsocketRouter::UnKnown: {
            // 未知类型不进入业务路由。
            return;
        }


    }




}
std::optional<ParsedNewMessage>WebSocketDispatcher::parseNewMessage(const nlohmann::json& payload,const Reply& reply) const {
    try {
        if (!payload.is_object()) {
            throw std::invalid_argument(
                "Payload must be a JSON object"
            );
        }

        ParsedNewMessage parsed;
        parsed.sender_device_id =requireUuid(payload, "sender_device_id");
        parsed.request.message_id =requireUuid(payload, "message_id");
        parsed.request.recipient_device_id =requireUuid(payload, "recipient_device_id");
        parsed.request.protocol_version =requireString(payload, "protocol_version");
        const auto& version = parsed.request.protocol_version;
        if (version.size() > 128 ||
            version.find_first_not_of(" \t\r\n") ==
                std::string::npos) {
            throw std::invalid_argument(
                "protocol_version is invalid"
            );
                }

        parsed.request.packet = requirePacket(payload);
        return parsed;
    } catch (const std::invalid_argument& error) {
        reply(nlohmann::json{
            {"type", "newTextMessage.result"},
            {"success", false},
            {"error", {
                {"code", "invalid_request"},
                {"message", error.what()}
            }}
        });
    } catch (const std::exception&) {
        reply(nlohmann::json{
            {"type", "newTextMessage.result"},
            {"success", false},
            {"error", {
                {"code", "internal_server_error"},
                {"message", "Unable to parse message"}
            }}
        });
    }

    return std::nullopt;
}
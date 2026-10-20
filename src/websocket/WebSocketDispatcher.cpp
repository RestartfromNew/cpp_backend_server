#include "common/Logger.h"
//
// Created by yangb on 2026/9/30.
//

#include "websocket/WebSocketDispatcher.h"

#include <sodium/utils.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <stdexcept>
#include "databases/DatabaseError.h"
#include <limits>
#include <set>

#include "service/ServiceThreadPool.h"
namespace {

std::string encodePacketBase64(const std::vector<std::uint8_t>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(4 * ((bytes.size() + 2) / 3));
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto remaining = bytes.size() - i;
        const std::uint32_t value = (static_cast<std::uint32_t>(bytes[i]) << 16U) |
            (remaining > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) << 8U : 0U) |
            (remaining > 2 ? bytes[i + 2] : 0U);
        encoded.push_back(alphabet[(value >> 18U) & 63U]);
        encoded.push_back(alphabet[(value >> 12U) & 63U]);
        encoded.push_back(remaining > 1 ? alphabet[(value >> 6U) & 63U] : '=');
        encoded.push_back(remaining > 2 ? alphabet[value & 63U] : '=');
    }
    return encoded;
}

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
    SyncEnvelope,
    AckEnvelope,
    UnKnown

};
WebsocketRouter parseType(const std::string& type) {
    if (type == "chat.newTextMessage_Test")
        return WebsocketRouter::NewTextMessageTest;
    if (type == "chat.newTextMessage")
        return WebsocketRouter::NewTextMessage;
    if (type=="chat.sync.envelope")
        return WebsocketRouter::SyncEnvelope;
    if (type == "chat.envelopes.ack")
        return WebsocketRouter::AckEnvelope;
    return WebsocketRouter::UnKnown;
}
WebSocketDispatcher::WebSocketDispatcher(ServiceThreadPool &threadPool,AuthMiddleWare &AuthMiddleWare,SessionIdentityService &session_identity_service):threadPool_(threadPool),AuthMiddleWare_(AuthMiddleWare),sessionIdentityService_(session_identity_service) {};
void WebSocketDispatcher::dispatch(const boost::uuids::uuid &session_device_id,const boost::uuids::uuid &session_user_id,nlohmann::json frame,Reply reply) {

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
    // JWT 本地验证留在入队前；设备关系查库位于各 Task 的 validateSessionRequest 中。
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
    if (userId!=session_user_id) {
        reply(nlohmann::json{
            {"type", "error"},
            {"error", {
                {"code", "accessToken_do_not_match_session"}
            }}
        });
        return;
    }

    nlohmann::json header=frame["header"];
    std::string payload_format= frame["payload_format"];
    // std::string device_id=payload["device_id"];
    // std::string receiver_device_id=payload["receiver_device_id"];
    const auto typeField = payload.find("type");
    if (typeField == payload.end() || !typeField->is_string()) {
        reply(nlohmann::json{{"type", "error"}, {"error", {{"code", "invalid_request"}}}});
        return;
    }
    const auto type = typeField->get<std::string>();
    switch (parseType(type)) {
        case WebsocketRouter::NewTextMessageTest: {
            //这里task是整个函数体，并没有执行函数
            Task task=[this,payload=std::move(payload),header=std::move(header),
                authResult,session_user_id,session_device_id,reply]()mutable {
                if (!validateSessionRequest(authResult, session_user_id, session_device_id, payload, reply)) return;
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
            try {
                threadPool_.receiveTask(std::move(task));
            } catch (const std::exception&) {
                reply(nlohmann::json{{"type", "newTextMessage.result"}, {"success", false},
                    {"error", {{"code", "server_busy"}, {"message", "Service task was not queued"}}}});
            }
            return;
        }
        case WebsocketRouter::NewTextMessage: {
            Task task=[this,payload=std::move(payload),header=std::move(header),userId,
                authResult,session_user_id,session_device_id,reply]()mutable {
                if (!validateSessionRequest(authResult, session_user_id, session_device_id, payload, reply)) return;
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
                    backend::logging::Logger::write(backend::logging::Level::Debug, "dispatcher", "send_result", {{"status", static_cast<int>(result.status)}});
                    switch (result.status) {
                        case NewTextMessageStatus::Success: {

                            if (!result.envelope) {
                                setError("internal_server_error", "Missing storage result");
                                break;
                            }
                            auto envelope=*(result.envelope);
                            response["success"] = true;
                            response["duplicate"] = false;
                            response["type"] = "newTextMessage.result";
                            response["message_id"] = boost::uuids::to_string(envelope.message_id);
                            response["envelope_id"] = boost::uuids::to_string(envelope.envelope_id);
                            nlohmann::json envelope_body{
                                {"message_id", boost::uuids::to_string(envelope.message_id)},
                                {"envelope_id", boost::uuids::to_string(envelope.envelope_id)},
                                {"sender_device_id",
                                    boost::uuids::to_string(envelope.sender_device_id)},
                                {"recipient_device_id",
                                    boost::uuids::to_string(envelope.recipient_device_id)},
                                {"protocol_version", envelope.protocol_version},
                                {"packet_base64", encodePacketBase64(envelope.packet)},
                                {"seq", envelope.seq}
                            };
                            response["seq"] = result.envelope->seq;
                            nlohmann::json push{
                                {"type", "Online_Sync"},
                                {"success", true},
                                {"duplicate", false},
                                {"recipientDeviceId", boost::uuids::to_string(envelope.recipient_device_id)},
                                {"seq", envelope.seq},
                                {"envelope", std::move(envelope_body)}
                            };
                            try {
                                reply(std::move(push));
                            } catch (const std::exception&) {
                                // Storage already committed; synchronization can recover delivery.
                            }
                            break;
                        }
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
        case WebsocketRouter::SyncEnvelope: {
            Task task=[this,payload=std::move(payload),header=std::move(header),userId,
                authResult,session_user_id,session_device_id,reply]()mutable {
                if (!validateSessionRequest(authResult, session_user_id, session_device_id, payload, reply)) return;
                const auto parsed = parseSyncEnvelope(payload, reply);
                if (!parsed) {
                    return; // 私有解析函数已经回复格式错误。
                }
                nlohmann::json response{
                    {"type", "chat.sync.envelope.result"},
                    {"success", false}
                };
                const auto setError = [&](const char* code, const char* message) {
                    response["error"] = {{"code", code}, {"message", message}};
                };
                try {
                    const auto page = threadPool_.chatService.syncEnvelopePage(userId, *parsed);
                    backend::logging::Logger::write(backend::logging::Level::Debug, "dispatcher", "sync_result", {{"success", page.has_value()}, {"count", page ? page->envelopes.size() : 0}});
                    if (!page) {
                        setError("sync_permission_denied", "Device must belong to the authenticated user and be active");
                    } else {
                        auto envelopes = nlohmann::json::array();
                        for (const auto& envelope : page->envelopes) {
                            envelopes.push_back({
                                {"envelope_id", boost::uuids::to_string(envelope.envelope_id)},
                                {"message_id", boost::uuids::to_string(envelope.message_id)},
                                {"sender_device_id", boost::uuids::to_string(envelope.sender_device_id)},
                                {"recipient_device_id", boost::uuids::to_string(envelope.recipient_device_id)},
                                {"protocol_version", envelope.protocol_version},
                                {"packet_base64", encodePacketBase64(envelope.packet)},
                                {"seq", envelope.seq}
                            });
                        }
                        response["success"] = true;
                        response["device_id"] = boost::uuids::to_string(parsed->device_id);
                        response["count"] = page->envelopes.size();
                        response["has_more"] = page->has_more;
                        response["envelopes"] = std::move(envelopes);
                    }
                } catch (const std::invalid_argument&) {
                    setError("invalid_request", "Invalid synchronization request");
                } catch (const DatabaseError& error) {
                    if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08")) {
                        setError("database_unavailable", "Service temporarily unavailable");
                    } else if (error.code() == "40001" || error.code() == "40P01") {
                        setError("retry_later", "Retry synchronization with the same cursors");
                    } else {
                        setError("internal_server_error", "Unable to synchronize envelopes");
                    }
                } catch (const std::exception&) {
                    response = {
                        {"type", "chat.sync.envelope.result"}, {"success", false},
                        {"error", {{"code", "internal_server_error"},
                                   {"message", "Unable to synchronize envelopes"}}}
                    };
                }
                reply(std::move(response));
            };
            try {

                threadPool_.receiveTask(std::move(task));
            } catch (const std::exception&) {
                reply(nlohmann::json{
                    {"type", "chat.sync.envelope.result"}, {"success", false},
                    {"error", {{"code", "server_busy"},
                               {"message", "Service task was not queued"}}}
                });
            }
            return;
        }
        case WebsocketRouter::AckEnvelope: {
            Task task=[this,payload=std::move(payload),userId,
                authResult,session_user_id,session_device_id,reply]()mutable {
                if (!validateSessionRequest(authResult, session_user_id, session_device_id, payload, reply)) return;
                const auto parsed = parseAckEnvelope(payload, reply);
                if (!parsed) return;
                nlohmann::json response{{"type", "chat.envelopes.ack.result"}, {"success", false}};
                const auto setError = [&](const char* code, const char* message) {
                    response["error"] = {{"code", code}, {"message", message}};
                };
                try {
                    const auto result = threadPool_.chatService.ackEnvelopes(userId, *parsed);
                    backend::logging::Logger::write(backend::logging::Level::Debug, "dispatcher", "ack_result", {{"success", result.has_value()}, {"count", result ? result->size() : 0}});
                    if (!result) {
                        setError("ack_permission_denied", "Device must belong to the authenticated user and be active");
                    } else {
                        auto ids = nlohmann::json::array();
                        for (const auto& id : *result) ids.push_back(boost::uuids::to_string(id));
                        response["device_id"] = boost::uuids::to_string(parsed->device_id);
                        response["acknowledged_ids"] = std::move(ids);
                        response["count"] = result->size();
                        response["success"] = true;
                    }
                } catch (const std::invalid_argument&) {
                    setError("invalid_request", "Invalid acknowledgment request");
                } catch (const DatabaseError& error) {
                    if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08"))
                        setError("database_unavailable", "Retry acknowledgment with the same identifiers");
                    else if (error.code() == "40001" || error.code() == "40P01")
                        setError("retry_later", "Retry acknowledgment with the same identifiers");
                    else setError("internal_server_error", "Unable to acknowledge envelopes");
                } catch (const std::exception&) {
                    response = {{"type", "chat.envelopes.ack.result"}, {"success", false},
                        {"error", {{"code", "internal_server_error"}, {"message", "Unable to acknowledge envelopes"}}}};
                }
                reply(std::move(response));
            };
            try {
                threadPool_.receiveTask(std::move(task));
            } catch (const std::exception&) {
                reply(nlohmann::json{{"type", "chat.envelopes.ack.result"}, {"success", false},
                    {"error", {{"code", "server_busy"}, {"message", "Service task was not queued"}}}});
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

std::optional<SyncEnvelopeRequest> WebSocketDispatcher::parseSyncEnvelope(
    const nlohmann::json& payload, const Reply& reply) const {
    try {
        if (!payload.is_object()) {
            throw std::invalid_argument("Payload must be a JSON object");
        }
        SyncEnvelopeRequest parsed;
        parsed.device_id = requireUuid(payload, "device_id");
        if (parsed.device_id.is_nil()) {
            throw std::invalid_argument("device_id must not be a nil UUID");
        }
        const auto cursors = payload.find("cursors");
        if (cursors == payload.end() || !cursors->is_array()) {
            throw std::invalid_argument("cursors must be an array; use [] for first synchronization");
        }
        if (cursors->size() > 1000) {
            throw std::invalid_argument("cursors must contain at most 1000 entries");
        }
        parsed.cursors.reserve(cursors->size());
        std::set<std::string> seenDevices;
        for (const auto& entry : *cursors) {
            if (!entry.is_object()) {
                throw std::invalid_argument("Each cursor must be an object");
            }
            const auto peerId = requireUuid(entry, "peer_device_id");
            if (peerId.is_nil() || peerId == parsed.device_id) {
                throw std::invalid_argument("peer_device_id must be non-nil and different from device_id");
            }
            if (!seenDevices.insert(boost::uuids::to_string(peerId)).second) {
                throw std::invalid_argument("peer_device_id must be unique within cursors");
            }
            const auto seq = entry.find("after_seq");
            if (seq == entry.end() || !seq->is_number_integer()) {
                throw std::invalid_argument("after_seq must be a non-negative BIGINT integer");
            }
            std::int64_t afterSeq;
            if (seq->is_number_unsigned()) {
                const auto value = seq->get<std::uint64_t>();
                if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument("after_seq exceeds BIGINT range");
                }
                afterSeq = static_cast<std::int64_t>(value);
            } else {
                afterSeq = seq->get<std::int64_t>();
                if (afterSeq < 0) {
                    throw std::invalid_argument("after_seq must not be negative");
                }
            }
            parsed.cursors.push_back(EnvelopeSyncCursor{
                .peer_device_id = peerId,
                .after_seq = afterSeq
            });
        }
        return parsed;
    } catch (const std::invalid_argument& error) {
        reply(nlohmann::json{
            {"type", "chat.sync.envelope.result"}, {"success", false},
            {"error", {{"code", "invalid_request"}, {"message", error.what()}}}
        });
    } catch (const std::exception&) {
        reply(nlohmann::json{
            {"type", "chat.sync.envelope.result"}, {"success", false},
            {"error", {{"code", "internal_server_error"},
                       {"message", "Unable to parse synchronization request"}}}
        });
    }
    return std::nullopt;
}

std::optional<AckEnvelopeRequest> WebSocketDispatcher::parseAckEnvelope(
    const nlohmann::json& payload, const Reply& reply) const {
    try {
        if (!payload.is_object()) throw std::invalid_argument("Payload must be a JSON object");
        AckEnvelopeRequest parsed;
        parsed.device_id = requireUuid(payload, "device_id");
        if (parsed.device_id.is_nil()) throw std::invalid_argument("device_id must not be nil");
        const auto ids = payload.find("envelope_ids");
        if (ids == payload.end() || !ids->is_array() || ids->empty() || ids->size() > 100)
            throw std::invalid_argument("envelope_ids must be an array of 1 to 100 UUIDs");
        std::set<std::string> seen;
        for (const auto& value : *ids) {
            const auto id = requireUuid(nlohmann::json{{"envelope_id", value}}, "envelope_id");
            if (id.is_nil() || !seen.insert(boost::uuids::to_string(id)).second)
                throw std::invalid_argument("envelope_ids must contain unique non-nil UUIDs");
            parsed.envelope_ids.push_back(id);
        }
        return parsed;
    } catch (const std::invalid_argument& error) {
        reply(nlohmann::json{{"type", "chat.envelopes.ack.result"}, {"success", false},
            {"error", {{"code", "invalid_request"}, {"message", error.what()}}}});
    } catch (const std::exception&) {
        reply(nlohmann::json{{"type", "chat.envelopes.ack.result"}, {"success", false},
            {"error", {{"code", "internal_server_error"}, {"message", "Unable to parse acknowledgment request"}}}});
    }
    return std::nullopt;
}
bool WebSocketDispatcher::validateSessionRequest(
    const AuthResult& authResult,
    const boost::uuids::uuid& session_user_id,
    const boost::uuids::uuid& session_device_id,
    const nlohmann::json& payload,
    const Reply& reply
) const {
    backend::logging::Scope operationLog{"WebSocketDispatcher", "validateSessionRequest"};
    const auto replyError = [&](const char* code) {
        backend::logging::Logger::write(backend::logging::Level::Warn, "dispatcher", "session_request_rejected", {{"code", code}});
        reply(nlohmann::json{
            {"type", "error"},
            {"error", {{"code", code}}}
        });
    };

    // 1. Access Token 验证结果。
    if (std::holds_alternative<AuthError>(authResult.result)) {
        const auto error = std::get<AuthError>(authResult.result);

        replyError(
            error == AuthError::Expired
                ? "access_token_expired"
                : "invalid_access_token"
        );
        return false;
    }

    // 2. Token 用户必须与连接绑定的用户一致。
    const auto& userId =
        std::get<boost::uuids::uuid>(authResult.result);

    if (userId != session_user_id) {
        replyError("accessToken_do_not_match_session");
        return false;
    }

    // 3. 设备归属及状态验证：这里仍会查询数据库。
    IdentityState identityResult;
    try {
        identityResult = sessionIdentityService_.verifySessionIdentity(userId, session_device_id);
    } catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08"))
            replyError("database_unavailable");
        else if (error.code() == "40001" || error.code() == "40P01")
            replyError("retry_later");
        else
            replyError("internal_server_error");
        return false;
    } catch (const std::exception&) {
        replyError("internal_server_error");
        return false;
    }

    if (identityResult != IdentityState::Success) {
        replyError("device_do_not_match_session");
        return false;
    }

    const auto typeField = payload.find("type");
    if (typeField == payload.end() || !typeField->is_string()) {
        replyError("invalid_request");
        return false;
    }

    const auto& type = typeField->get_ref<const std::string&>();

    const char* deviceField = nullptr;
    const char* responseType = nullptr;

    if (type == "chat.newTextMessage") {
        deviceField = "sender_device_id";
        responseType = "newTextMessage.result";
    } else if (type == "chat.sync.envelope") {
        deviceField = "device_id";
        responseType = "chat.sync.envelope.result";
    } else if (type == "chat.envelopes.ack") {
        deviceField = "device_id";
        responseType = "chat.envelopes.ack.result";
    }

    if (deviceField == nullptr) {
        return true; // 其他类型仍交给原来的路由判断。
    }

    boost::uuids::uuid requestDeviceId;
    try {
        requestDeviceId = requireUuid(payload, deviceField);
    } catch (const std::invalid_argument&) {
        reply(nlohmann::json{
            {"type", responseType},
            {"success", false},
            {"error", {
                {"code", "invalid_request"},
                {"message", "A valid session device UUID is required"}
            }}
        });
        return false;
    }

    if (requestDeviceId != session_device_id) {
        reply(nlohmann::json{
            {"type", responseType},
            {"success", false},
            {"error", {
                {"code", "session_device_mismatch"},
                {"message",
                 "Request device must match this WebSocket session"}
            }}
        });
        return false;
    }

    return true;
}
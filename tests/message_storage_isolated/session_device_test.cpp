#include "websocket/WebSocketDispatcher.h"
#include "service/ServiceThreadPool.h"
#include <iostream>
#include <stdexcept>

int main() {
    DatabasePool pool;
    EnvelopeRepository repository(pool);
    ChatService service(repository);
    ServiceThreadPool threads(1, service);
    AuthMiddleWare auth;
    SessionIdentityService identity;
    WebSocketDispatcher dispatcher(threads, auth, identity);
    const boost::uuids::uuid user{"11111111-1111-4111-8111-111111111111"};
    const boost::uuids::uuid device{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
    const std::string otherDevice = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    int cases = 0;
    const auto check = [&](nlohmann::json payload, const char* expected, const char* resultType) {
        nlohmann::json response;
        int replies = 0;
        dispatcher.dispatch(device, user,
            {{"header", nlohmann::json::object()}, {"payload_format", "json"}, {"payload", payload}},
            [&](nlohmann::json value) { response = std::move(value); ++replies; });
        if (replies != 1 || response["error"]["code"] != expected ||
            response["type"] != resultType || pool.connection.calls != 0) {
            throw std::runtime_error("Session binding case failed: " + std::to_string(cases + 1));
        }
        ++cases;
    };
    for (const auto* type : {"chat.newTextMessage", "chat.sync.envelope", "chat.envelopes.ack"}) {
        const bool send = std::string{type} == "chat.newTextMessage";
        const char* field = send ? "sender_device_id" : "device_id";
        const std::string resultType = send ? "newTextMessage.result" : std::string{type} + ".result";
        nlohmann::json payload{{"type", type}, {"authorization", "Bearer valid"}, {field, otherDevice}};
        check(payload, "session_device_mismatch", resultType.c_str());
        payload.erase(field);
        check(payload, "invalid_request", resultType.c_str());
        payload[field] = 123;
        check(payload, "invalid_request", resultType.c_str());
        payload[field] = "not-a-uuid";
        check(payload, "invalid_request", resultType.c_str());
    }
    identity.state = IdentityState::NotActivated;
    check({{"type", "chat.sync.envelope"}, {"authorization", "Bearer valid"}, {"device_id", device.text}},
        "device_do_not_match_session", "error");
    std::cout << "PASS: " << cases << " session device checks (simulated identity/database).\n";
}

//
// Created by yangb on 2026/9/28.
//

#ifndef CPP_BACKEND_SERVER_WEBSOCKETSESSION_H
#define CPP_BACKEND_SERVER_WEBSOCKETSESSION_H
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <nlohmann/json_fwd.hpp>
#include "nlohmann/json.hpp"
#include "server/Connection.h"
enum class Opcode : std::uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA
};

struct Frame {
    //一个websocket帧的结构
    //Fin
    bool fin = false;
    //类型
    Opcode opcode = Opcode::Continuation;
    // Already unmasked payload bytes.
    //可以把uint8——t转化为文本，图片等
    std::vector<std::uint8_t> payload;
};
enum class FrameParseStatus {
    Success, Error,NeedMoreData
};
struct FrameParseResult {
    FrameParseStatus status;
    std::size_t consumed;
    std::vector<Frame> frames;
};

class WebSocketSession {
public:
    using Clock = std::chrono::steady_clock;
    WebSocketSession(std::uint64_t id, Connection&& connection) noexcept;
    [[nodiscard]] std::uint64_t id() const noexcept;
    Connection& connection() noexcept;
    //frame解析器
    FrameParseResult FrameParse(std::string_view bytes);
    //frame编码器
    std::vector<std::uint8_t> FrameEncoder(const Frame &frame);
    nlohmann::json ApplicationParse(const Frame& frame) const;
    void recordActivity() noexcept;
    void handlePong() noexcept;
    int getFd();
    //留下心跳检测机制
    bool shouldSendPing(Clock::time_point now,std::chrono::seconds ping_interval) ;
    bool PongTimeout(Clock::time_point now,std::chrono::seconds pong_timeout) ;
    void markPingSent(Clock::time_point now);


private:
    std::uint64_t id_;
    Connection connection_;
    //最后一次接收到客户端消息,包括pong和message
    Clock::time_point last_activity_{
        Clock::now()
    };
    //最近一次发送ping之间
    Clock::time_point ping_sent_at_{};
    //是否正在等待pong
    bool waiting_for_pong_ = false;
    //ping的id
    std::uint64_t ping_id_ = 0;
};


#endif //CPP_BACKEND_SERVER_WEBSOCKETSESSION_H

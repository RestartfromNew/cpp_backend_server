//
// Created by yangb on 2026/9/28.
//

#include "websocket/WebSocketSession.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

WebSocketSession::WebSocketSession(
    std::uint64_t id,
    Connection&& connection
) noexcept
    : id_(id), connection_(std::move(connection)) {}

std::uint64_t WebSocketSession::id() const noexcept {
    return id_;
}

Connection& WebSocketSession::connection() noexcept {
    return connection_;
}

void WebSocketSession::recordActivity() noexcept {
    last_activity_ = Clock::now();
}

void WebSocketSession::handlePong() noexcept {
    std::cout<<"收到pong"<<std::endl;
    waiting_for_pong_ = false;
    last_activity_ = Clock::now();
}

FrameParseResult WebSocketSession::FrameParse(std::string_view bytes) {
    constexpr std::uint64_t MaxPayloadSize = 1024U * 1024U;

    std::size_t consumed = 0;
    std::vector<Frame> frames;

    while (true) {
        const std::string_view sub_buffer = bytes.substr(consumed);

        if (sub_buffer.size() < 2) {
            return {
                frames.empty() ? FrameParseStatus::NeedMoreData : FrameParseStatus::Success,
                consumed,
                std::move(frames)
            };
        }

        const auto first = static_cast<std::uint8_t>(
            static_cast<unsigned char>(sub_buffer[0])
        );
        const auto second = static_cast<std::uint8_t>(
            static_cast<unsigned char>(sub_buffer[1])
        );

        const bool fin = (first & 0x80U) != 0;
        const bool reservedBitsSet = (first & 0x70U) != 0;
        const auto opcodeValue = static_cast<std::uint8_t>(first & 0x0FU);
        const bool masked = (second & 0x80U) != 0;

        if (reservedBitsSet || !masked) {
            return {FrameParseStatus::Error, consumed, {}};
        }

        Opcode opcode;
        switch (opcodeValue) {
            case 0x0U: opcode = Opcode::Continuation; break;
            case 0x1U: opcode = Opcode::Text; break;
            case 0x2U: opcode = Opcode::Binary; break;
            case 0x8U: opcode = Opcode::Close; break;
            case 0x9U: opcode = Opcode::Ping; break;
            case 0xAU: opcode = Opcode::Pong; break;
            default:
                return {FrameParseStatus::Error, consumed, {}};
        }

        std::size_t headerSize = 2;
        std::uint64_t payloadSize = second & 0x7FU;

        if (payloadSize == 126U) {
            if (sub_buffer.size() < 4) {
                return {
                    frames.empty() ? FrameParseStatus::NeedMoreData : FrameParseStatus::Success,
                    consumed,
                    std::move(frames)
                };
            }

            payloadSize =
                (static_cast<std::uint64_t>(static_cast<unsigned char>(sub_buffer[2])) << 8U) |
                static_cast<std::uint64_t>(static_cast<unsigned char>(sub_buffer[3]));
            headerSize = 4;

            if (payloadSize < 126U) {
                return {FrameParseStatus::Error, consumed, {}};
            }
        } else if (payloadSize == 127U) {
            if (sub_buffer.size() < 10) {
                return {
                    frames.empty() ? FrameParseStatus::NeedMoreData : FrameParseStatus::Success,
                    consumed,
                    std::move(frames)
                };
            }

            if ((static_cast<unsigned char>(sub_buffer[2]) & 0x80U) != 0) {
                return {FrameParseStatus::Error, consumed, {}};
            }

            payloadSize = 0;
            for (std::size_t i = 2; i < 10; ++i) {
                payloadSize =
                    (payloadSize << 8U) |
                    static_cast<std::uint64_t>(
                        static_cast<unsigned char>(sub_buffer[i])
                    );
            }
            headerSize = 10;

            if (payloadSize <= 65535U) {
                return {FrameParseStatus::Error, consumed, {}};
            }
        }

        const bool controlFrame = opcodeValue >= 0x8U;
        if ((controlFrame && (!fin || payloadSize > 125U)) ||
            (opcode == Opcode::Close && payloadSize == 1U) ||
            payloadSize > MaxPayloadSize) {
            return {FrameParseStatus::Error, consumed, {}};
        }

        constexpr std::size_t MaskSize = 4;
        if (sub_buffer.size() < headerSize + MaskSize) {
            return {
                frames.empty() ? FrameParseStatus::NeedMoreData : FrameParseStatus::Success,
                consumed,
                std::move(frames)
            };
        }

        const std::size_t frameSize =
            headerSize + MaskSize + static_cast<std::size_t>(payloadSize);
        if (sub_buffer.size() < frameSize) {
            return {
                frames.empty() ? FrameParseStatus::NeedMoreData : FrameParseStatus::Success,
                consumed,
                std::move(frames)
            };
        }

        const std::size_t maskOffset = headerSize;
        const std::size_t payloadOffset = headerSize + MaskSize;
        Frame frame;
        frame.fin = fin;
        frame.opcode = opcode;
        frame.payload.resize(static_cast<std::size_t>(payloadSize));

        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            const auto encodedByte = static_cast<std::uint8_t>(
                static_cast<unsigned char>(sub_buffer[payloadOffset + i])
            );
            const auto maskByte = static_cast<std::uint8_t>(
                static_cast<unsigned char>(sub_buffer[maskOffset + (i % MaskSize)])
            );
            frame.payload[i] = static_cast<std::uint8_t>(encodedByte ^ maskByte);
        }
        frames.push_back(std::move(frame));
        consumed += frameSize;

        if (consumed == bytes.size()) {
            return {FrameParseStatus::Success, consumed, std::move(frames)};
        }
    }
}

std::vector<std::uint8_t> WebSocketSession::FrameEncoder(const Frame& frame) {
    std::vector<std::uint8_t> encoded;
    const std::uint64_t payloadSize = frame.payload.size();

    std::size_t headerSize = 2;
    if (payloadSize > 125U && payloadSize <= 65535U) {
        headerSize += 2;
    } else if (payloadSize > 65535U) {
        headerSize += 8;
    }
    encoded.reserve(headerSize + frame.payload.size());

    const auto opcode = static_cast<std::uint8_t>(frame.opcode);
    encoded.push_back(static_cast<std::uint8_t>(
        (frame.fin ? 0x80U : 0x00U) | opcode
    ));

    if (payloadSize <= 125U) {
        encoded.push_back(static_cast<std::uint8_t>(payloadSize));
    } else if (payloadSize <= 65535U) {
        encoded.push_back(126U);
        encoded.push_back(static_cast<std::uint8_t>((payloadSize >> 8U) & 0xFFU));
        encoded.push_back(static_cast<std::uint8_t>(payloadSize & 0xFFU));
    } else {
        encoded.push_back(127U);
        for (int shift = 56; shift >= 0; shift -= 8) {
            encoded.push_back(static_cast<std::uint8_t>(
                (payloadSize >> static_cast<unsigned int>(shift)) & 0xFFU
            ));
        }
    }

    encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
    return encoded;
}

nlohmann::json WebSocketSession::ApplicationParse(const Frame& frame) const {
    //opcode_name=text，payload_formate是json, payload中是json,不然就是普通text,
    //如果不是，payload保存字节数组，formate为byte
    const char* opcodeName = "unknown";
    switch (frame.opcode) {
        case Opcode::Continuation: opcodeName = "continuation"; break;
        case Opcode::Text:         opcodeName = "text"; break;
        case Opcode::Binary:       opcodeName = "binary"; break;
        case Opcode::Close:        opcodeName = "close"; break;
        case Opcode::Ping:         opcodeName = "ping"; break;
        case Opcode::Pong:         opcodeName = "pong"; break;
    }

    nlohmann::json result{
        {"header", {
            {"fin", frame.fin},
            {"opcode", static_cast<std::uint8_t>(frame.opcode)},
            {"opcode_name", opcodeName},
            {"payload_length", frame.payload.size()}
        }}
    };

    if (frame.opcode == Opcode::Text) {
        const std::string text(frame.payload.begin(), frame.payload.end());
        nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);

        if (parsed.is_discarded()) {
            result["payload_format"] = "text";
            result["payload"] = text;
        } else {
            result["payload_format"] = "json";
            result["payload"] = std::move(parsed);
        }
    } else {
        result["payload_format"] = "bytes";
        result["payload"] = frame.payload;
    }

    return result;
}

int WebSocketSession::getFd() {
    return connection_.fd();
}

bool WebSocketSession::shouldSendPing(Clock::time_point now,std::chrono::seconds ping_interval) {
    if (waiting_for_pong_==false&& now-last_activity_ > ping_interval)
        return true;
    return false;
}
bool WebSocketSession::PongTimeout(Clock::time_point now,std::chrono::seconds pong_timeout) {
    return waiting_for_pong_ && now-ping_sent_at_ >= pong_timeout;
}
void WebSocketSession::markPingSent(Clock::time_point now) {
    waiting_for_pong_ = true;
    ping_sent_at_ = now;
    ping_id_++;

}

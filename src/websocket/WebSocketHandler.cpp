//
// Created by yangb on 2026/9/28.
//
#include "websocket/WebSocketHandler.h"

#include <cerrno>
#include <string_view>
#include <utility>

WebSocketHandleResult WebSocketMessageHandler::handleReadable(
    WebSocketSession& session
) {
    WebSocketHandleResult result{
        WebSocketHandleStatus::NeedMoreData,
        {},
        false
    };

    Connection& connection = session.connection();

    while (true) {
        //这里被worker调用，从connection中真正读取缓冲数据
        const ssize_t bytesRead = connection.read();

        if (bytesRead > 0) {
            session.recordActivity();
            //解析
            FrameParseResult parsed =session.FrameParse(connection.inputBuffer());
            if (parsed.consumed > 0) {
                connection.consumeInput(parsed.consumed);
            }
            if (parsed.status == FrameParseStatus::Error) {
                result.status = WebSocketHandleStatus::ProtocolError;
                return result;
            }
            for (Frame& frame : parsed.frames) {
                if (!handleFrame(session, std::move(frame), result)) {
                    return result;
                }
            }
            continue;
        }

        if (bytesRead == 0) {
            result.status = WebSocketHandleStatus::PeerClosed;
            return result;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!result.frames.empty()) {
                result.status = WebSocketHandleStatus::MessagesReady;
            }

            return result;
        }

        result.status = WebSocketHandleStatus::IoError;
        return result;
    }
}

bool WebSocketMessageHandler::handleFrame(
    WebSocketSession& session,
    Frame&& frame,
    WebSocketHandleResult& result
) {
    switch (frame.opcode) {
        case Opcode::Text:
        case Opcode::Binary:
            if (!frame.fin) {
                result.status = WebSocketHandleStatus::ProtocolError;
                return false;
            }

            result.frames.push_back(std::move(frame));

            return true;

        case Opcode::Ping: {
            Frame pong;
            pong.fin = true;
            pong.opcode = Opcode::Pong;
            pong.payload = std::move(frame.payload);

            const std::vector<std::uint8_t> encoded =
                session.FrameEncoder(pong);

            session.connection().write(std::string_view{
                reinterpret_cast<const char*>(encoded.data()),
                encoded.size()
            });

            result.output_queued = true;
            return true;
        }

        case Opcode::Pong:
            session.handlePong();
            return true;

        case Opcode::Close: {
            Frame closeReply;
            closeReply.fin = true;
            closeReply.opcode = Opcode::Close;
            closeReply.payload = std::move(frame.payload);

            const std::vector<std::uint8_t> encoded =
                session.FrameEncoder(closeReply);

            session.connection().write(std::string_view{
                reinterpret_cast<const char*>(encoded.data()),
                encoded.size()
            });

            result.output_queued = true;
            result.status = WebSocketHandleStatus::PeerClosed;
            return false;
        }

        case Opcode::Continuation:
            result.status = WebSocketHandleStatus::ProtocolError;
            return false;
    }
    result.status = WebSocketHandleStatus::ProtocolError;
    return false;
}

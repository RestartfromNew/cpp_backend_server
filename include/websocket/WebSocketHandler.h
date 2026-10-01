#ifndef CPP_BACKEND_SERVER_WEBSOCKETHANDLER_H
#define CPP_BACKEND_SERVER_WEBSOCKETHANDLER_H

#include <cstdint>
#include <vector>

#include "websocket/WebSocketSession.h"

enum class WebSocketHandleStatus {
    MessagesReady,
    NeedMoreData,
    PeerClosed,
    ProtocolError,
    IoError
};

struct WebSocketHandleResult {
    WebSocketHandleStatus status;
    std::vector<Frame> frames;
    bool output_queued = false;
};

class WebSocketMessageHandler {
public:
    WebSocketHandleResult handleReadable(WebSocketSession& session);

private:
    bool handleFrame(
        WebSocketSession& session,
        Frame&& frame,
        WebSocketHandleResult& result
    );
};

#endif

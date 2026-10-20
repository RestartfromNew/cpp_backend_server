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

/** @brief 协议读取结果和完整业务帧集合；output_queued 提示 Worker 开启可写事件。 */
struct WebSocketHandleResult {
    WebSocketHandleStatus status;
    std::vector<Frame> frames;
    bool output_queued = false;
};

/** @brief 处理单 Session 的协议读写及控制帧；业务 JSON 由 Dispatcher 处理，不在此访问业务数据库。 */
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

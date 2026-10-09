//
// Created by yangb on 2026/9/30.
//

#include "websocket/WebSocketDispatcher.h"

#include "service/ServiceThreadPool.h"
enum class WebsocketRouter {
    //发一条新消息
    NewTextMessageTest,
    UnKnown

};
WebsocketRouter parseType(const std::string& type) {
    if (type == "chat.newTextMessage_Test")
        return WebsocketRouter::NewTextMessageTest;
    return WebsocketRouter::UnKnown;
}
WebSocketDispatcher::WebSocketDispatcher(ServiceThreadPool &threadPool):threadPool_(threadPool) {};
void WebSocketDispatcher::dispatch(nlohmann::json frame,Reply reply) {
    nlohmann::json payload=std::move(frame["payload"]);
    nlohmann::json header=frame["header"];
    std::string payload_format= frame["payload_format"];
    std::string device_id=payload["device_id"];
    std::string receiver_device_id=payload["receiver_device_id"];
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
        case WebsocketRouter::UnKnown: {
            reply=std::move(reply);
            return;
        }

    }




}
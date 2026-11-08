//
// Created by yangb on 2026/11/3.
//

#ifndef CPP_BACKEND_SERVER_UPLOADREACTOR_H
#define CPP_BACKEND_SERVER_UPLOADREACTOR_H
#include <functional>
#include <domain/UploadSession.h>
#include <atomic>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <cerrno>
#include <system_error>
#include <utility>
#include <string>
#include <mutex>
#include <unordered_map>
#include <file/DiskOperation.h>
#include <queue>
#include <file/DiskPool.h>

class UploadReactor {
private:
    using Task=std::function<void()>;
    DiskPool &disk_pool_;
    int port_;
    DiskOperation disk_operation_;
    //注册epoll，epoll的fd
    int epoll_fd_=-1;
    //接收磁盘任务完成的通知
    int disk_task_completion_event_fd_=-1;
    int business_fd_=-1;
    std::unordered_map<int,UploadSession> sessionMap;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    //incoming_connection_listen_fd_接收新连接
    std::uint64_t next_generation_ = 1;
    UniqueFd incoming_connection_listen_fd_;
    std::queue<DiskWriteResult> disk_pool_task_response_queue_;

    void postWriteResult(DiskWriteResult disk_write_result);


    //启动新文件监听
    void createListenSocket();
    void work();
    void listen_fd_connection();
    void readRequestHeaders(int connection_socket_fd);
    void dispatchRequest(int connection_socket_fd, UploadSession& session);
    void requestUploadAuthorization(int connection_socket_fd); // TODO：后续接入 Service 异步权限检查。
    void queueResponse(int connection_socket_fd, int status, const std::string& code);
    void flushResponse(int connection_socket_fd);
    void removeSession(int connection_socket_fd);


public:
    UploadReactor(int port,DiskPool &disk_pool);
    ~UploadReactor();
    bool init();
    void run(); // 阻塞运行事件循环；由外部管理线程调用，不在构造函数中阻塞。
    void close(); // 唤醒并停止 run()；所有者等 run() 返回后再销毁对象。
    void createFile();
    void pushWrite();
    bool submitWrite(UploadSession& session);



};


#endif //CPP_BACKEND_SERVER_UPLOADREACTOR_H

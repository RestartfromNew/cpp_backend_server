//
// Created by yangb on 2026/11/3.
//

#ifndef CPP_BACKEND_SERVER_UPLOADREACTOR_H
#define CPP_BACKEND_SERVER_UPLOADREACTOR_H
#include <functional>
#include <memory>
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

#include "http/AuthMiddleWare.h"
#include "domain/UploadPermission.h"

class UploadReactor {
private:
    using Task=std::function<void()>;
    DiskPool &disk_pool_;
    int port_;
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
    AuthMiddleWare &auth_middle_ware_;
    DiskOperation disk_operation_;
    struct VerificationCompletion {
        WriteTaskIdentity identity;
        VerifyPermissionResult result;
    };
    struct VerificationQueue {
        std::mutex mutex;
        std::queue<VerificationCompletion> completions;
    };
    std::shared_ptr<VerificationQueue> verification_queue_ = std::make_shared<VerificationQueue>();

    void postWriteResult(DiskWriteResult disk_write_result);


    //启动新文件监听
    void createListenSocket();
    void work();
    void listen_fd_connection();
    void readRequestHeaders(int connection_socket_fd);
    void dispatchRequest(int connection_socket_fd, UploadSession& session);
    VerifyPermissionResult requestUploadAuthorization(int connection_socket_fd);
    void queueResponse(int connection_socket_fd, int status, const std::string& code);
    void flushResponse(int connection_socket_fd);
    void removeSession(int connection_socket_fd);
    bool submitVerificationRequest(WriteTaskIdentity identity, const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& upload_id, const std::string& upload_token, std::uint64_t content_length);
    void handleVerificationResult(int connection_socket_fd, VerifyPermissionResult result);
    void drainVerificationResults();


public:
    UploadReactor(int port, DiskPool& disk_pool, AuthMiddleWare& auth_middle_ware, FileService& file_service);
    ~UploadReactor();
    bool init();
    void run(); // 阻塞运行事件循环；由外部管理线程调用，不在构造函数中阻塞。
    void close(); // 唤醒并停止 run()；所有者等 run() 返回后再销毁对象。
    void createFile();
    void pushWrite();
    bool submitWrite(UploadSession& session);



};


#endif //CPP_BACKEND_SERVER_UPLOADREACTOR_H

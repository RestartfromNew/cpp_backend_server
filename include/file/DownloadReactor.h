//
// Created by yangb on 2026/11/16.
//

#ifndef CPP_BACKEND_SERVER_DOWNLOADREACTOR_H
#define CPP_BACKEND_SERVER_DOWNLOADREACTOR_H
#include <functional>
#include <memory>
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
#include "domain/DownloadSession.h"

class DownloadReactor {
private:
    using Task=std::function<void()>;
    DiskPool &disk_pool_;
    int port_;
    //注册epoll，epoll的fd
    int epoll_fd_=-1;
    //接收磁盘任务完成的通知
    int disk_task_completion_event_fd_=-1;
    int business_fd_=-1;
    std::unordered_map<int,DownloadSession> sessionMap;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    //incoming_connection_listen_fd_接收新连接
    std::uint64_t next_generation_ = 1;
    std::queue<DownloadDiskReadResult> disk_pool_task_response_queue_;
    AuthMiddleWare &auth_middle_ware_;
    UniqueFd incoming_connection_listen_fd_;
    FileService& file_service_;
    DiskOperation &disk_operation_;


    void work();
    void listen_fd_connection();
    void readRequestHeaders(int connection_socket_fd);
    void queueResponse(int connection_socket_fd, int status, const std::string& code);
    void createListenSocket() ;
    void flushResponse(int connection_socket_fd);
    bool submitOpenFileRequest(DownloadSession& session);
    void removeSession(int connection_socket_fd);
    void postReadResult(DownloadDiskReadResult disk_read_result);
    bool submitRead(DownloadSession& session);
    void flushDownload(int connection_socket_fd);
public:
    DownloadReactor(int port, DiskPool &disk_pool, AuthMiddleWare &auth_middle_ware, FileService &file_service,DiskOperation &disk_operation);
    ~DownloadReactor();
    bool init();

    void run(); // 阻塞运行事件循环；由外部管理线程调用，不在构造函数中阻塞。
    void close(); // 唤醒并停止 run()；所有者等 run() 返回后再销毁对象。
};


#endif //CPP_BACKEND_SERVER_DOWNLOADREACTOR_H

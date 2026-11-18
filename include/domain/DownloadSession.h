//
// Created by yangb on 2026/11/16.
//

#ifndef CPP_BACKEND_SERVER_DOWNLOADSESSION_H
#define CPP_BACKEND_SERVER_DOWNLOADSESSION_H
#include "common/UniqueFd.h"
#include "domain/FileError.h"
#include "http/HttpRequest.h"
#include "domain/UploadPermission.h"

#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>
enum class DownloadHttpState {
    ReadingHeader,        // 累计并解析请求头。
    WaitingAuthorization, // 等待权限校验和打开文件的任务结果。
    Downloading,          // 先发成功响应头，再按块读盘和发送正文。
    SendingResponse,      // 发送错误响应，结束后关闭连接。
    Finished
};
struct DownloadSession {
    boost::uuids::uuid file_id{}; // 要下载的 attachment ID
    boost::uuids::uuid uploader_id;
    boost::uuids::uuid user_id{}; // 从认证 token 获取。
    boost::uuids::uuid device_id{}; // 查库核对归属和 active 状态。
    std::optional<ClaimedUpload> claim;
    std::string storage_key;
    std::string final_path;
    std::uint64_t generation = 0;  // 防止 socket fd 复用造成误操作
    UniqueFd socket;               // 拥有网络连接
    std::shared_ptr<UniqueFd> file; // 下载文件 fd；打开任务完成前为空
    std::uint64_t expected_file_bytes = 0; // 预期的文件大小
    std::uint64_t read_file_bytes = 0; // Reactor 已核对成功的累计读盘字节数，用作下一块文件偏移
    std::uint64_t sent_bytes = 0;  // 已交给下载连接 socket 的文件正文字节数，排除 HTTP 响应头
    std::chrono::steady_clock::time_point body_deadline{};
    bool read_in_progress = false;  // 当前是否有磁盘读取任务
    std::size_t sending_buffer_bytes = 0; // 当前正文块总字节数；尚未发送量为此值减 block_sent
    DownloadHttpState http_state = DownloadHttpState::ReadingHeader;
    std::string header_buffer; // 只累计有界请求头；不在这里收集完整文件正文。
    HttpRequest download_request; // 解析后的请求头，身份仍未验证。
    std::uint64_t declared_bytes = 0; // 客户端 Content-Length，与 Service 批准大小分开。
    std::vector<char> download_response_pending_body; // 待发送缓冲区
    std::string response_buffer; // 非阻塞连接 socket 可能只发送部分响应。
    std::size_t response_sent = 0; // response_buffer 的发送偏移，专用于 HTTP 响应头或错误响应
    std::size_t block_sent = 0; // 当前正文块的发送偏移，每块发完归零
};
struct DownloadTaskIdentity {
    int download_request_fd=-1;
    std::uint64_t generation=0;
};
enum class DownloadWorkType {
    OpenFile,ReadDisk
};
struct DownloadTask {
    DownloadTaskIdentity identity;
    boost::uuids::uuid uploader_id;
    boost::uuids::uuid user_id{}; // 从认证 token 获取。
    boost::uuids::uuid device_id{}; // 查库核对归属和 active 状态。
    boost::uuids::uuid file_id;
    std::shared_ptr<UniqueFd> file;
    std::uint64_t offset = 0;
    std::uint64_t expected_data = 0; // 本次任务最多读取的字节数
    std::string storage_key; //相对路径
};
struct DownloadDiskReadResult {
    DownloadWorkType work_type;
    DownloadTaskIdentity identity;
    std::uint64_t file_size_bytes = 0;
    std::uint64_t offset = 0;
    std::uint64_t read_bytes = 0; // 本次实际读取量
    std::optional<FileError> file_error; // 保留业务错误以映射 HTTP 状态。
    std::error_code error;           // 为空表示对应磁盘任务成功。
    std::shared_ptr<UniqueFd> opened_file; // OpenReadFile 成功时持有下载文件的文件 fd。
    std::vector<char> data;
};
#endif //CPP_BACKEND_SERVER_DOWNLOADSESSION_H

//
// Created by yangb on 2026/11/3.
//

#ifndef CPP_BACKEND_SERVER_UPLOADSESSION_H
#define CPP_BACKEND_SERVER_UPLOADSESSION_H
#include "common/UniqueFd.h"
#include "http/HttpRequest.h"
#include "domain/UploadPermission.h"

#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
enum class UploadHttpState { ReadingHeaders, WaitingAuthorization, SendingResponse, ReceivingBody, Finalizing };
struct UploadSession {
    boost::uuids::uuid upload_id{}; // 数据库上传任务 ID
    std::optional<ClaimedUpload> claim;
    std::string temp_path;
    std::string final_path;
    std::uint64_t generation = 0;  // 防止 socket fd 复用造成误操作
    UniqueFd socket;               // 拥有网络连接
    std::shared_ptr<UniqueFd> file; // 临时文件；创建完成前为空
    std::uint64_t expected_bytes = 0; // 批准接收的字节数
    std::uint64_t received_bytes = 0; // 已接收的字节数
    std::uint64_t written_bytes = 0;  // 已成功写入的字节数
    std::chrono::steady_clock::time_point body_deadline{};
    bool write_in_progress = false;  // 当前是否有磁盘写入任务
    std::size_t pending_write_bytes = 0; // 当前在途数据块的长度，供完成结果核对。
    UploadHttpState http_state = UploadHttpState::ReadingHeaders;
    std::string header_buffer; // 只累计有界请求头；不在这里收集完整文件正文。
    HttpRequest upload_request; // 解析后的请求头，身份和上传凭据仍未验证。
    std::uint64_t declared_bytes = 0; // 客户端 Content-Length，与 Service 批准大小分开。
    std::vector<char> pending_body; // 与请求头同次 recv 到达的少量正文，权限通过前不写文件。
    std::string response_buffer; // 非阻塞连接 socket 可能只发送部分响应。
    std::size_t response_sent = 0;
};

struct WriteTaskIdentity {
    int connection_socket_fd=-1; // 上传连接 socket fd，仅用于对应结果，不用于写文件。
    std::uint64_t generation=0;
};
struct DiskWriteTask {
    //Reactor向disk投递的参数
    WriteTaskIdentity identity;
    std::shared_ptr<UniqueFd> file; // 持有文件 fd，保证执行期间文件仍打开。
    std::uint64_t offset = 0;       // 本块在文件中的起始位置。
    std::vector<char> data;        // 本次写入的数据，由任务持有。
    std::string temp_path;          // OpenTempFile 使用的服务端生成路径。
};
enum DiskWorkType {
    WriteDisk,OpenFile
};
// 磁盘池 → Reactor。
struct DiskWriteResult {
    DiskWorkType work_type;
    WriteTaskIdentity identity;
    std::uint64_t offset = 0; // 本块开始写入的位置，用于核对当前写入进度。
    std::uint64_t written_bytes = 0; // 本次实际写入量，包括失败前的部分写入。
    std::error_code error;           // 为空表示整块写入成功。
    std::shared_ptr<UniqueFd> opened_file; // OpenTempFile 成功时持有新建临时文件的文件 fd。
};
#endif //CPP_BACKEND_SERVER_UPLOADSESSION_H

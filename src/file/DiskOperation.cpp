//
// Created by yangb on 2026/11/4.
//

#include "file/DiskOperation.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>
DiskOperation::DiskOperation(FileService& file_service) : file_service_(file_service) {}
DiskWriteResult DiskOperation::writeBlock(const DiskWriteTask& task) {
    DiskWriteResult result{};
    result.identity = task.identity;
    //偏移量
    result.offset = task.offset;

    if (!task.file || !task.file->valid()) {
        //如果指针为空或者说文件为空
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }

    // pwrite 使用 off_t，需要保证偏移和数据范围可以表示。
    const auto max_offset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());

    if (task.offset > max_offset || task.data.size() > max_offset - task.offset) {
        //是否超过了可以写入的最大值
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }

    std::size_t completed = 0;

    while (completed < task.data.size()) {
        const auto remaining = task.data.size() - completed;

        const auto count = std::min(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));

        // 使用文件 fd，不使用监听 socket fd 或连接 socket fd。
        //从data已经完成的completed偏移量开始写
        const ssize_t written = ::pwrite(task.file->get(), task.data.data() + completed, count, static_cast<off_t>(task.offset + completed));

        if (written == -1) {
            const int error = errno;

            if (error == EINTR) {
                //信号中断
                continue;
            }

            //其它的error
            result.error = std::error_code(error, std::generic_category());
            break;
        }

        if (written == 0) {
            // 没有进展，避免无限循环。
            result.error = std::make_error_code(std::errc::io_error);
            break;
        }

        completed += static_cast<std::size_t>(written);
    }

    result.written_bytes = completed;
    return result;
}

DiskWriteResult DiskOperation::OpenTempFile(const DiskWriteTask& task) {
    DiskWriteResult result{};
    result.identity = task.identity;

    if (task.temp_path.empty() || task.temp_path.find('\0') != std::string::npos) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    // 先分配所有权对象，避免创建文件后因内存分配失败留下空文件。
    auto file = std::make_shared<UniqueFd>();
    int file_fd;
    do {
        file_fd = ::open(task.temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    } while (file_fd == -1 && errno == EINTR);

    if (file_fd == -1) {
        const int error = errno;
        result.error = std::error_code(error, std::generic_category());
        return result;
    }

    file->reset(file_fd);
    result.opened_file = std::move(file);
    return result;
}
VerifyPermissionResult DiskOperation::verifyPermission(const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& upload_id, const std::string& upload_token, const std::uint64_t &content_length) {
    return file_service_.verifyUploadPermission(user_id, upload_id, upload_token, content_length);
}

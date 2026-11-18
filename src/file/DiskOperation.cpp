#include <sys/stat.h>
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
    result.work_type=DiskWorkType::WriteDisk;
    result.written_bytes = completed;
    return result;
}
DownloadDiskReadResult  DiskOperation::ReadBlock(const DownloadTask& task) {
    DownloadDiskReadResult result{};
    result.identity = task.identity;
    result.offset = task.offset;
    result.work_type=DownloadWorkType::ReadDisk;
    if (!task.file || !task.file->valid()) {
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        return result;
    }
    // pread 使用 off_t，保证文件偏移和本次读取范围可以表示。
    const auto max_offset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (task.offset > max_offset || task.expected_data > max_offset - task.offset) {
        result.error = std::make_error_code(std::errc::value_too_large);
        return result;
    }
    std::size_t completed = 0;
    while (completed < task.expected_data) {
        const auto remaining = task.expected_data - completed;
        char buffer[8192];
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, sizeof(buffer)));
        // 使用文件 fd，不使用监听 socket fd 或连接 socket fd。
        //从data已经完成的completed偏移量开始写
        const ssize_t read = ::pread(task.file->get(), buffer, count, static_cast<off_t>(task.offset+completed));
        if (read == -1) {
            const int error = errno;
            if (error == EINTR) {
                //信号中断
                continue;
            }
            //其它的error
            result.error = std::error_code(error, std::generic_category());
            break;
        }
        if (read == 0) {
            // 没有进展，避免无限循环。
            result.error = std::make_error_code(std::errc::io_error);
            break;
        }
        result.data.insert(result.data.end(), buffer, buffer + read);
        completed += static_cast<std::size_t>(read);
    }
    result.read_bytes = completed;
    return result;


}
DiskWriteResult DiskOperation::OpenTempFile(const DiskWriteTask& task) {
    DiskWriteResult result{};
    result.work_type = DiskWorkType::OpenFile;
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
DownloadDiskReadResult DiskOperation::OpenReadFile(const DownloadTask& task) {
    DownloadDiskReadResult result{};
    result.work_type=DownloadWorkType::OpenFile;
    result.identity = task.identity;

    auto storage_key=file_service_.getFileStorageKey(task.file_id, task.uploader_id, task.user_id, task.device_id);

    if (const auto* error = std::get_if<FileError>(&storage_key)) {
        result.file_error = *error;
        switch (*error) {
            case FileError::NoSuchFile:
            case FileError::FileDeleted:
                result.error = std::make_error_code(std::errc::no_such_file_or_directory);
                break;
            case FileError::PermissionDenied:
            case FileError::FileExpired:
                result.error = std::make_error_code(std::errc::permission_denied);
                break;
            case FileError::FileNotReady:
                result.error = std::make_error_code(std::errc::resource_unavailable_try_again);
                break;
            case FileError::LookupFailed:
            default:
                result.error = std::make_error_code(std::errc::io_error);
                break;
        }
        return result;
    }

    // 数据库元数据确定下载路径和预期正文长度。
    auto file = std::make_shared<UniqueFd>();
    const char* configured_root = std::getenv("FILE_STORAGE_ROOT");
    const std::filesystem::path root{configured_root && *configured_root ? configured_root : "/srv/chat-files"};
    const auto& metadata = std::get<FileStorageInfo>(storage_key);
    const auto& key = metadata.storage_key;
    const auto full_path = root / key;
    int file_fd;
    do {
        file_fd = ::open(full_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (file_fd == -1 && errno == EINTR);
    if (file_fd == -1) {
        const int error = errno;
        result.error = std::error_code(error, std::generic_category());
        return result;
    }
    file->reset(file_fd);
    struct stat file_stat{};
    if (::fstat(file->get(), &file_stat) == -1) {
        result.error = std::error_code(errno, std::generic_category());
        return result;
    }
    if (!S_ISREG(file_stat.st_mode) || file_stat.st_size < 0 ||
        static_cast<std::uint64_t>(file_stat.st_size) != metadata.size_bytes) {
        result.error = std::make_error_code(std::errc::io_error);
        return result;
    }
    result.file_size_bytes = metadata.size_bytes;
    result.opened_file = std::move(file);
    return result;

}

//
// Created by yangb on 2026/11/4.
//

#ifndef CPP_BACKEND_SERVER_DISKOPERATION_H
#define CPP_BACKEND_SERVER_DISKOPERATION_H
#include "domain/DownloadSession.h"
#include "domain/UploadSession.h"
#include "domain/UploadPermission.h"
#include "service/FileService.h"
#include <cstdlib>
#include <filesystem>

class DiskOperation {
private:
    FileService& file_service_;
public:
    explicit DiskOperation(FileService& file_service);
    [[nodiscard]] static DiskWriteResult writeBlock(const DiskWriteTask& task);
    [[nodiscard]] DownloadDiskReadResult ReadBlock(const DownloadTask& task);
    [[nodiscard]] static DiskWriteResult OpenTempFile(const DiskWriteTask& task);
    [[nodiscard]] DownloadDiskReadResult OpenReadFile(const DownloadTask& task);
    [[nodiscard]] VerifyPermissionResult verifyPermission(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& upload_id, const std::string& upload_token, const std::uint64_t &content_length);
};


#endif //CPP_BACKEND_SERVER_DISKOPERATION_H

//
// Created by yangb on 2026/11/11.
//

#ifndef CPP_BACKEND_SERVER_FILESERVICE_H
#define CPP_BACKEND_SERVER_FILESERVICE_H
#include "domain/FileError.h"
#include "domain/FileStorageInfo.h"
#include "repository/FileRepository.h"

class FileService {

public:
    explicit FileService(FileRepository& fileRepository);
    std::variant<UploadPermissionError, UploadPermissionGranted> createUploadCredentials(
        const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id, const UploadPermissionRequest& request);
    [[nodiscard]] VerifyPermissionResult verifyUploadPermission(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& upload_id, const std::string& upload_token, std::uint64_t content_length);
    std::variant<VerifyPermissionResult, ClaimedUpload> claimUpload(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& upload_id, const std::string& upload_token, std::uint64_t content_length);
    bool finishUpload(const boost::uuids::uuid& upload_id, std::uint64_t size, const std::string& digest);
    void failUpload(const boost::uuids::uuid& upload_id, const std::string& code);
    [[nodiscard]] std::variant<FileError, FileStorageInfo> getFileStorageKey(const boost::uuids::uuid& attachment_id, const boost::uuids::uuid& uploader_id, const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id);


private:
    FileRepository& fileRepository_;
};


#endif //CPP_BACKEND_SERVER_FILESERVICE_H

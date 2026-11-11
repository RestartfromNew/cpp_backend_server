//
// Created by yangb on 2026/11/11.
//

#ifndef CPP_BACKEND_SERVER_FILEREPOSITORY_H
#define CPP_BACKEND_SERVER_FILEREPOSITORY_H

#include "databases/DatabasePool.h"
#include "domain/UploadPermission.h"

class FileRepository {
public:
    explicit FileRepository(DatabasePool& pool);
    // token_hash_hex 为 Service 计算的 SHA-256 十六进制字符串。新任务的 token 原文由 Service 填入。
    std::variant<UploadPermissionError, UploadPermissionGranted> createFileUploadPermission(
        const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id,
        const std::string& token_hash_hex, const UploadPermissionRequest& request);

private:
    DatabasePool& pool_;
};


#endif //CPP_BACKEND_SERVER_FILEREPOSITORY_H

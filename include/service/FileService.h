//
// Created by yangb on 2026/11/11.
//

#ifndef CPP_BACKEND_SERVER_FILESERVICE_H
#define CPP_BACKEND_SERVER_FILESERVICE_H
#include "repository/FileRepository.h"

class FileService {
public:
    explicit FileService(FileRepository& fileRepository);
    std::variant<UploadPermissionError, UploadPermissionGranted> createUploadCredentials(
        const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id, const UploadPermissionRequest& request);

private:
    FileRepository& fileRepository_;
};


#endif //CPP_BACKEND_SERVER_FILESERVICE_H

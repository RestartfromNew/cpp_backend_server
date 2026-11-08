//
// Created by yangb on 2026/11/4.
//

#ifndef CPP_BACKEND_SERVER_DISKOPERATION_H
#define CPP_BACKEND_SERVER_DISKOPERATION_H
#include "domain/UploadSession.h"

class DiskOperation {
public:
    [[nodiscard]] static DiskWriteResult writeBlock(const DiskWriteTask& task);
    [[nodiscard]] static DiskWriteResult OpenTempFile(const DiskWriteTask& task);
};


#endif //CPP_BACKEND_SERVER_DISKOPERATION_H

//
// Created by yangb on 2026/9/22.
//

#ifndef CPP_BACKEND_SERVER_CNNECTIONLEASE_H
#define CPP_BACKEND_SERVER_CNNECTIONLEASE_H

#include "databases/DatabaseConnection.h"
#include "databases/DatabasePool.h"
class ConnectionLease {
    public:
    ConnectionLease(DatabasePool &pool):pool_(pool) {
        connection_ = pool_.getConnection();
    };
    ~ConnectionLease() noexcept {
      pool_.releaseReturnedConnections(std::move(connection_));
    };
    ConnectionLease(const ConnectionLease&) = delete;
    ConnectionLease(ConnectionLease &&) = delete;
    ConnectionLease &operator=(ConnectionLease &&) = delete;
    ConnectionLease &operator=(const ConnectionLease &) = delete;
    [[nodiscard]]
    DatabaseConnection& connection() noexcept
    {
        return *connection_;
    }

    [[nodiscard]]
    const DatabaseConnection& connection() const noexcept
    {
        return *connection_;
    }
    private:
    std::unique_ptr<DatabaseConnection> connection_;
    DatabasePool &pool_;

};


#endif //CPP_BACKEND_SERVER_CNNECTIONLEASE_H

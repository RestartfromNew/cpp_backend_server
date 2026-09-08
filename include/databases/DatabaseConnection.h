//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_DATABASECONNECTION_H
#define CPP_BACKEND_SERVER_DATABASECONNECTION_H
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <libpq-fe.h>
#include "databases/DatabaseResult.h"
/**
 * @brief Manages a PostgreSQL database connection.
 *
 * @note DatabaseConnection owns the underlying PGconn resource and releases it
 * automatically when the object is destroyed.
 *
 * The connection is non-copyable or movable.
 */

class DatabaseConnection {
public:
    /**
 * @brief Establishes a PostgreSQL connection.
 *
 * @param connectionString PostgreSQL connection string used to connect
 *        to the database.
 * Example:host=127.0.0.1 port=5432 dbname=psql_username user=cpp_backend_app password=password
 */
    explicit DatabaseConnection(
        const std::string& connectionString
    );

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;

    DatabaseConnection(DatabaseConnection&&) noexcept = delete;
    DatabaseConnection& operator=(
        DatabaseConnection&&
    ) noexcept = delete;

    [[nodiscard]]
    /**
 * @brief Executes a parameterized SQL statement.
 *
 * @param sql SQL statement to execute.
 * @param parameters Parameters bound to the SQL statement.
 *
 * @return DatabaseResult containing the result returned by PostgreSQL.
 *
 * @throws std::runtime_error If the query execution fails.
 */
    DatabaseResult execute(
        std::string_view sql,
        std::span<const std::string> parameters = {}
    );

private:
    struct Deleter {
        void operator()(PGconn* connection) const noexcept;
    };

    std::unique_ptr<PGconn, Deleter> connection_;
};



#endif //CPP_BACKEND_SERVER_DATABASECONNECTION_H

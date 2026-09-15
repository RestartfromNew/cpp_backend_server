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
#include <functional>
#include <type_traits>
#include <utility>
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
    DatabaseConnection& operator=(DatabaseConnection&&) noexcept = delete;


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
    DatabaseResult execute(std::string_view sql,std::span<const std::string> parameters = {});
    template <typename Function> auto withTransaction(Function&& operation)-> std::invoke_result_t<Function&&>
    {
        using Result = std::invoke_result_t<Function&&>;
        // 不允许返回引用，避免结果依赖回调内部的局部对象。
        static_assert(!std::is_reference_v<Result>,"Transaction callback must return a value or void");
        // 减少提交成功后，移动返回值却抛异常的风险。
        static_assert(std::is_void_v<Result> ||std::is_nothrow_move_constructible_v<Result>,"Transaction result must be nothrow movable");
        //从这里开始执行
        beginTransaction();
        try {
            if constexpr (std::is_void_v<Result>) {
                //调用传入的函数
                std::invoke(std::forward<Function>(operation));
                commitTransaction();
                //提交
            } else {
                Result result =std::invoke(std::forward<Function>(operation));
                commitTransaction();
                return result;
            }
        }
        catch (...) {
            rollbackTransactionNoThrow();
            // 原始错误继续交给业务层处理。
            throw;
        }
    }

private:
    struct Deleter {
        void operator()(PGconn* connection) const noexcept;
    };

    std::unique_ptr<PGconn, Deleter> connection_;
    void beginTransaction();
    void commitTransaction();
    void rollbackTransactionNoThrow() noexcept;
    void requireConnection(std::string_view operation);

};



#endif //CPP_BACKEND_SERVER_DATABASECONNECTION_H

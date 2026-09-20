//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_DATABASERESULTS_H
#define CPP_BACKEND_SERVER_DATABASERESULTS_H


#include <cstddef>
#include <memory>
#include <string_view>
#include <libpq-fe.h>
/**
 * @brief Manage a Database execution result
 *
 * @note DatabaseResult owns the underlying PGconn resource and releases it.
 * automatically when the object is destroyed.
 *
 * The connection is non-copyable or movable.
 */

class DatabaseResult {
public:
    explicit DatabaseResult(PGresult* result);
    //四大复制都不允许

    DatabaseResult(const DatabaseResult&) = delete;
    DatabaseResult& operator=(const DatabaseResult&) = delete;

    DatabaseResult(DatabaseResult&&) noexcept = default;
    DatabaseResult& operator=(DatabaseResult&&) noexcept = default;

    /**
     *
     * @return Return the number of rows and columns in the result
     */
    [[nodiscard]]
    std::size_t rowCount() const noexcept;

    [[nodiscard]]
    std::size_t columnCount() const noexcept;

    [[nodiscard]]
    bool isNull(
        std::size_t row,
        std::size_t column
    ) const;

    [[nodiscard]]

    /**
     *@brief return the value of Database result of one cell
     */
    std::string_view value(
        std::size_t row,
        std::size_t column
    ) const;

private:
    struct Deleter {
        void operator()(PGresult* result) const noexcept;
    };

    std::unique_ptr<PGresult, Deleter> result_;
};


#endif //CPP_BACKEND_SERVER_DATABASERESULTS_H

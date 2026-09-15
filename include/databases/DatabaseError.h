//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_DATABASEERROR_H
#define CPP_BACKEND_SERVER_DATABASEERROR_H
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
enum class DatabaseErrorKind {
    Connection,
    Query,
    Constraint,
    DataConversion
};
class DatabaseError :public std::runtime_error{
    public:
    explicit DatabaseError(DatabaseErrorKind kind, std::string code, std::string message,std::string constraint);
    explicit DatabaseError(DatabaseErrorKind kind, std::string code, std::string message);

    [[nodiscard]]
    DatabaseErrorKind kind() const noexcept;

    [[nodiscard]]
    std::string_view code() const noexcept;
    [[nodiscard]]
    std::optional<std::string> constraint() const noexcept;
private:
    //kind用于上层程序决定处理策略
    DatabaseErrorKind kind_;
    //psql原始错误识别，用于诊断
    std::string code_;
    //constraint用于反应具体的数据库错误
    std::optional<std::string> constraint_= std::nullopt;
};


#endif //CPP_BACKEND_SERVER_DATABASEERROR_H

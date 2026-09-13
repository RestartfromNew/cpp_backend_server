//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_DATABASEERROR_H
#define CPP_BACKEND_SERVER_DATABASEERROR_H
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
    explicit DatabaseError(DatabaseErrorKind kind, std::string code, std::string message);

    [[nodiscard]]
    DatabaseErrorKind kind() const noexcept;

    [[nodiscard]]
    std::string_view code() const noexcept;
private:
    //kind用于上层程序决定处理策略
    DatabaseErrorKind kind_;
    //psql原始错误识别，用于诊断
    std::string code_;
};


#endif //CPP_BACKEND_SERVER_DATABASEERROR_H

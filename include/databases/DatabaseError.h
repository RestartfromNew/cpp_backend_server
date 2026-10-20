//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_DATABASEERROR_H
#define CPP_BACKEND_SERVER_DATABASEERROR_H
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
// 基础设施/查询/约束/结果转换分类；HTTP 映射和是否重试由上层决定。
enum class DatabaseErrorKind {
    Connection,
    Query,
    Constraint,
    DataConversion
};
/** @brief 数据库异常携带 kind、诊断 code 和可选约束名；上层映射业务错误，日志只取代码，不输出 SQL 或参数。 */
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

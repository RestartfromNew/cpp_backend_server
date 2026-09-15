//
// Created by yangb on 2026/9/2.
//

#include "databases/DatabaseConnection.h"
#include <string>
#include <vector>

#include "databases/DatabaseError.h"

void DatabaseConnection::Deleter::operator()(PGconn* connection) const noexcept
{
    if (connection != nullptr) {
        PQfinish(connection);
    }
}

DatabaseConnection::DatabaseConnection(const std::string& connectionString): connection_(PQconnectdb(connectionString.c_str())){
    //构建connection,返回一个Psql的智能指针connection_

    //没有成功获取到连接
    if (!connection_) {
        throw DatabaseError{DatabaseErrorKind::Connection,"ALLOCATION_FAILED","Failed to allocate PostgreSQL connection"};
    }
    //获取到连接，但是连接数据库是失败
    if (PQstatus(connection_.get()) != CONNECTION_OK) {
        const std::string message =PQerrorMessage(connection_.get());
        connection_.reset();
        throw DatabaseError(DatabaseErrorKind::Connection,"CONNECTION_FAILED",message);
    }
}

void DatabaseConnection::requireConnection(std::string_view operation) {
    //在执行前检查数据库连接
    if (!connection_) {
        // 已经为空，不需要 reset()。
        throw DatabaseError{
            DatabaseErrorKind::Connection,
            "CONNECTION_UNAVAILABLE",
            std::string{operation} + ": connection is null",
        };
    }
    if (PQstatus(connection_.get()) != CONNECTION_OK) {
        // 必须在释放连接前复制诊断信息。
        const std::string detail{PQerrorMessage(connection_.get())};
        // 按你当前策略，坏连接直接弃用。
        connection_.reset();
        throw DatabaseError{
            DatabaseErrorKind::Connection,
            "CONNECTION_FAILED",
            std::string{operation} + ": " + detail,
        };
    }

}

DatabaseResult DatabaseConnection::execute(std::string_view sql,std::span<const std::string> parameters)
{
    requireConnection("DatabaseConnection::execute");
    //执行单条sql语句
    std::vector<const char*> parameterValues;
    parameterValues.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        parameterValues.push_back(parameter.c_str());
    }

    const std::string sqlText{sql};
    //执行查询，返回的结果是一个指向数据库结果的指针，所以有可能为空
    PGresult* rawResult = PQexecParams(
        connection_.get(),
        sqlText.c_str(),
        static_cast<int>(parameterValues.size()),
        nullptr,
        parameterValues.empty()
            ? nullptr
            : parameterValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (rawResult == nullptr) {
        throw DatabaseError{DatabaseErrorKind::Query,"NULL_RESULT",
            "DatabaseConnection::execute: " +std::string{PQerrorMessage(connection_.get())},
        };
    }
    //执行的结果保存在DatabaseResult中
    DatabaseResult result{rawResult};
    const ExecStatusType status =
        PQresultStatus(rawResult);
    if (status != PGRES_TUPLES_OK &&status != PGRES_COMMAND_OK) {
        //结果是存在的，但是命令没有执行成功
        //如果执行途中出现任何的错误，都会在这里抛出，记录在constrainName中
        const char* sqlState =PQresultErrorField(rawResult, PG_DIAG_SQLSTATE);
        const char* constraintName =PQresultErrorField(rawResult, PG_DIAG_CONSTRAINT_NAME);
        throw DatabaseError{DatabaseErrorKind::Query,
            sqlState ? sqlState : "QUERY_FAILED",
            "PostgreSQL query failed: " +
                std::string{PQresultErrorMessage(rawResult)},
            constraintName ? constraintName : ""
        };
    }

    return result;
}

void DatabaseConnection::beginTransaction()
{
    requireConnection("DatabaseConnection::beginTransaction");
    //连接成功，但是有正在执行的其它事务
    if (PQtransactionStatus(connection_.get()) != PQTRANS_IDLE) {
        throw DatabaseError{
            DatabaseErrorKind::Query,
            "TRANSACTION_NOT_IDLE",
            "DatabaseConnection::beginTransaction: "
            "connection already has an active or failed transaction",
            ""
        };
    }
    try {
        execute("BEGIN");
    }
    catch (...) {
        //出现错误，向更上层抛出错误
        // BEGIN 失败时，保守地放弃该连接。
        connection_.reset();
        throw;
    }
}

void DatabaseConnection::commitTransaction()
{
    requireConnection("DatabaseConnection::commitTransaction");
    if (PQtransactionStatus(connection_.get()) != PQTRANS_INTRANS) {
        throw DatabaseError{
            DatabaseErrorKind::Query,
            "TRANSACTION_NOT_COMMITTABLE",
            "DatabaseConnection::commitTransaction: "
            "transaction is not in a committable state",
            ""
        };
    }
    execute("COMMIT");
}

void DatabaseConnection::rollbackTransactionNoThrow() noexcept
{
    if (!connection_) {
        return;
    }
    try {
        execute("ROLLBACK");

        // 回滚完成后，连接应该恢复为空闲状态。
        if (PQstatus(connection_.get()) != CONNECTION_OK ||PQtransactionStatus(connection_.get()) != PQTRANS_IDLE) {
            connection_.reset();
        }
    }
    catch (...) {
        // 不能覆盖原来的业务/数据库异常。
        // 关闭连接，阻止后续复用不确定的事务状态。
        connection_.reset();

        // 有日志设施后，在这里记录回滚失败。
        // 日志操作自身也必须不向外抛异常。
    }
}
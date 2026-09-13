//
// Created by yangb on 2026/9/2.
//

#include "databases/DatabaseConnection.h"
#include <string>
#include <vector>

#include "databases/DatabaseError.h"

void DatabaseConnection::Deleter::operator()(
    PGconn* connection
) const noexcept
{
    if (connection != nullptr) {
        PQfinish(connection);
    }
}

DatabaseConnection::DatabaseConnection(const std::string& connectionString): connection_(PQconnectdb(connectionString.c_str())){
    //构建connection,返回一个Psql的智能指针connection_

    if (!connection_) {
        throw DatabaseError{DatabaseErrorKind::Connection,"ALLOCATION_FAILED","Failed to allocate PostgreSQL connection"};
    }
    if (PQstatus(connection_.get()) != CONNECTION_OK) {
        const std::string message =PQerrorMessage(connection_.get());
        throw DatabaseError(DatabaseErrorKind::Connection,message,"Failed to connect to PostgreSQL connection");
    }
}

DatabaseResult DatabaseConnection::execute(
    std::string_view sql,
    std::span<const std::string> parameters
)
{
    //执行psql语句
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
            "PostgreSQL returned a null result: " +std::string{PQerrorMessage(connection_.get())}};
    }
    //执行的结果保存在DatabaseResult中
    DatabaseResult result{rawResult};

    const ExecStatusType status =
        PQresultStatus(rawResult);

    if (status != PGRES_TUPLES_OK &&status != PGRES_COMMAND_OK) {
        //结果是存在的，但是命令没有执行成功
        const char* sqlState =PQresultErrorField(rawResult,PG_DIAG_SQLSTATE);
        const std::string code =sqlState == nullptr? "QUERY_FAILED": std::string{sqlState};

        throw DatabaseError{DatabaseErrorKind::Query,code,"PostgreSQL query failed: " +std::string{PQresultErrorMessage(rawResult)}
        };
    }

    return result;
}
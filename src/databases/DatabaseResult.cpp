//
// Created by yangb on 2026/9/4.
//

#include "databases/DatabaseResult.h"

#include <stdexcept>
//数据库查询结果，不负责理解

DatabaseResult::DatabaseResult(PGresult* result)
    : result_(result)
{
    //执行了查询之后，会返回一个Psql结果表的指针，交给result_管理
    if (!result_) {
        throw std::invalid_argument(
            //不能允许空指针
            "DatabaseResult cannot own a null PGresult"
        );
    }
}

void DatabaseResult::Deleter::operator()(
    PGresult* result
) const noexcept
{
    //自定义的特殊删除器
    if (result != nullptr) {
        PQclear(result);
        //这是一个资源，所以要clear,释放资源
    }
}

std::size_t DatabaseResult::rowCount() const noexcept
{
    return static_cast<std::size_t>(
        //libpq的函数，返回结果的行数，返回的结果定义是int, 用static_cast强制转换
        PQntuples(result_.get())
        //PQntuples应该传入PGresult指针，用get给psql使用
    );
}

std::size_t DatabaseResult::columnCount() const noexcept
{
    return static_cast<std::size_t>(
        //同理
        PQnfields(result_.get())
    );
}

bool DatabaseResult::isNull(
    std::size_t row,
    std::size_t column
) const
{
    //检查第几行第几列的值是不是null，比如username=Null，不是“”，这是字符串
    return PQgetisnull(
        result_.get(),
        static_cast<int>(row),
        static_cast<int>(column)
    ) != 0;
}

std::string_view DatabaseResult::value(
    std::size_t row,
    std::size_t column
) const
{
    if (isNull(row, column)) {
        //先检查是否为空
        throw std::logic_error(
            "Cannot read a null database value"
        );
    }

    const auto rowIndex = static_cast<int>(row);
    const auto columnIndex = static_cast<int>(column);

    const char* data =
        //psql返回的是结果内部的char*指针，是一个地址
        PQgetvalue(result_.get(), rowIndex, columnIndex);

    const int length =
        //长度
        PQgetlength(result_.get(), rowIndex, columnIndex);

    return {
        data,
        static_cast<std::size_t>(length)
    };
}
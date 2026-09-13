//
// Created by yangb on 2026/9/4.
//

#include "databases/DatabaseError.h"

DatabaseError::DatabaseError(DatabaseErrorKind kind, std::string code, std::string message)
:kind_(kind), code_(code), std::runtime_error(message){};
DatabaseErrorKind
DatabaseError::kind() const noexcept
{
    return kind_;
}

std::string_view
DatabaseError::code() const noexcept
{
    return code_;
}
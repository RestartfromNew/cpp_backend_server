//
// Created by yangb on 2026/9/4.
//

#include "databases/DatabaseError.h"

DatabaseError::DatabaseError(DatabaseErrorKind kind, std::string code, std::string message,std::string constraint)
:kind_(kind), code_(code), std::runtime_error(message), constraint_(constraint){};
DatabaseError::DatabaseError(DatabaseErrorKind kind, std::string code, std::string message):kind_(kind), code_(code), std::runtime_error(message){}
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

std::optional<std::string> DatabaseError::constraint() const noexcept {
    return constraint_;
}

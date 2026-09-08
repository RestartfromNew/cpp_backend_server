//
// Created by yangb on 2026/9/4.
//

#include "databases/DatabaseError.h"

DatabaseError::DatabaseError(const std::string& message): std::runtime_error(message){}
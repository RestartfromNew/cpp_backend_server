//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_DATABASEERROR_H
#define CPP_BACKEND_SERVER_DATABASEERROR_H
#include <stdexcept>
#include <string>

class DatabaseError :public std::runtime_error{
    public:
    explicit DatabaseError(const std::string& message);
};


#endif //CPP_BACKEND_SERVER_DATABASEERROR_H

//
// Created by yangb on 2026/9/7.
//

#ifndef CPP_BACKEND_SERVER_CONNECTION_H
#define CPP_BACKEND_SERVER_CONNECTION_H
#include "common/UniqueFd.h"
#include <string>
#include <errno.h>
#include <iostream>

class Connection {
    public:
    explicit Connection(UniqueFd&& connection)noexcept;
    ~Connection()noexcept=default;
    //不允许移动和复制
    Connection(const Connection& other)=delete;
    Connection& operator=(const Connection& other)=delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept=default;

    ssize_t read();
    void write(std::string_view data);
    ssize_t flush();
    std::string_view input()const noexcept;
    void consume(std::size_t n);
    bool is_open()const noexcept;
    int fd()const noexcept;
    void close()noexcept;
    void sendAll(std::string_view data);
    std::string_view inputBuffer() const ;

    void consumeInput(std::size_t n);



private:
    UniqueFd fd_;
    std:: string input_buffer_;
    std:: string output_buffer_;
    bool state_=true;
};


#endif //CPP_BACKEND_SERVER_CONNECTION_H

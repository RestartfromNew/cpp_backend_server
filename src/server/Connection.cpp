//
// Created by yangb on 2026/9/7.
//

#include "server/Connection.h"

#include <sys/socket.h>

Connection::Connection(UniqueFd&& connection) noexcept:fd_(std::move(connection)) {

}

ssize_t Connection::read() {
    char buffer[1024]{};
    while (true) {
        ssize_t n=recv(fd_.get(),buffer,1024,0);
        if (n>0) {
            input_buffer_.append(buffer, static_cast<std::size_t>(n));
            return n;
        }

        if (n==0) {
            state_=false;
            return 0;
        }

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //
            }

        }
        return -1;
    }


}

void Connection::write(std::string_view data) {
    //write只负责追加
    output_buffer_.append(data.data(), data.size());
}
ssize_t Connection::flush() {
    //真正负责发送

    if (output_buffer_.size()==0)
        return 0;
    while (true) {
        ssize_t n=send(fd_.get(),output_buffer_.data(),output_buffer_.size(),MSG_NOSIGNAL
);
        if (n>0)
            output_buffer_.erase(0,static_cast<std::size_t>(n));
        if (n<0&&errno==EINTR)continue;
        return n;
    }

}

void Connection::consume(std::size_t n) {
    if (n > input_buffer_.size()) {
        throw std::out_of_range("consume exceeds input buffer size");
        n = input_buffer_.size();
    }

    input_buffer_.erase(0, n);
}

std::string_view Connection::input()const noexcept{
    return input_buffer_;
}
bool Connection::is_open()const noexcept {
    return state_;
}
int Connection::fd()const noexcept {
    return fd_.get();
}
void Connection::close()noexcept {
    fd_.reset();
    state_=false;
}
void Connection::sendAll(std::string_view data) {
    write(data);
    while (!output_buffer_.empty()) {
        ssize_t n = flush();

        if (n > 0) {
            continue;
        }

        if (n == 0) {
            throw std::runtime_error("socket closed while sending");
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }

        throw std::runtime_error("send failed");
    }
}
std::string_view Connection::inputBuffer() const {
    return input_buffer_;
}

void Connection::consumeInput(std::size_t n) {
    if (n >= input_buffer_.size()) {
        input_buffer_.clear();
        return;
    }

    input_buffer_.erase(0, n);
}
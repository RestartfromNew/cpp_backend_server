//
// Created by yangb on 2026/9/7.
//

#include "common/UniqueFd.h"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <unistd.h>
#include <iostream>

UniqueFd::UniqueFd()noexcept=default;
UniqueFd::UniqueFd(int fd)noexcept :fd_(fd) {}
UniqueFd::~UniqueFd()noexcept {if (fd_ != -1) ::close(fd_);}
UniqueFd::UniqueFd(UniqueFd&& other) noexcept:fd_(other.fd_) {
    other.fd_ = -1;
}
UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        if (fd_ != -1) ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}


[[nodiscard]]int UniqueFd::get()const noexcept {
    return fd_;
}
[[nodiscard]]bool UniqueFd::valid()const noexcept {
    if (fd_ == -1) return false;
    return true;
}
int UniqueFd::release() noexcept {
    int old_fd = this->fd_;
    fd_ = -1;
    return old_fd;

}
void UniqueFd::reset(int new_fd) noexcept {
    if (fd_ != -1) ::close(fd_);
    fd_ = new_fd;
}
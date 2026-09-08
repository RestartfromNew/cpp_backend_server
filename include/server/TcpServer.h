//
// Created by yangb on 2026/9/1.
//
#include "common/UniqueFd.h"

#pragma once
#include <string>
class TcpServer {
public:
    TcpServer(const std::string & ip,int port);
    ~TcpServer();
    void start();
    UniqueFd acceptConnection();
private:
    void createSocket();
    void bindSocket();
    void listenSocket();
    // void acceptLoop();

    std::string ip_;
    int port_;
    UniqueFd server_fd_;
};



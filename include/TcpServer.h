//
// Created by yangb on 2026/9/1.
//

#ifndef CPP_BACKEND_SERVER_TCPSERVER_H
#define CPP_BACKEND_SERVER_TCPSERVER_H

#pragma once
#include <string>
class TcpServer {
public:
    TcpServer(const std::string & ip,int port);
    ~TcpServer();
    void start();
private:
    void createSocket();
    void bindSocket();
    void listenSocket();
    void acceptLoop();
private:
    std::string ip_;
    int port_;
    int server_fd_;
};


#endif //CPP_BACKEND_SERVER_TCPSERVER_H

//
// Created by yangb on 2026/9/1.
//

#include "../../include/TcpServer.h"
#include <arpa/inet.h>
//ip地址转换和字节序转换
#include <sys/socket.h>
//核心头文件
#include <unistd.h>
//为了关闭文件描述符的fd
#include <cstring>
#include <iostream>
#include <stdexcept>
TcpServer::TcpServer(const std::string& ip,int port): ip_(ip),port_(port),server_fd_(-1) {}
//server_fd等于-1表示当前没有有效的socket状态，大量Linux api用-1表示无效状态
TcpServer::~TcpServer() {
    if (server_fd_ != -1) {
        //表示资源的ownship
        close(server_fd_);
    }
}
void TcpServer::start() {
    createSocket();
    bindSocket();
    listenSocket();
    acceptLoop();
}
void TcpServer::createSocket() {
    //表示ipv4或者ipv6,address family;type表示面向字节流；0表示根据前面两个参数自动选择协议
    server_fd_ = socket(
        AF_INET,
        SOCK_STREAM,
        0

    );
    //AF+socket stream 通常就表示TCP，返回一个整数的索引，表示fd，表示拿到的Linux用户态句柄
    std::cout<<"创建socket，server_fd="<<server_fd_<<std::endl;


    if (server_fd_ == -1) {
        //如果没有拿到就表示失败
        throw std::runtime_error(
            "Failed to create socket"
        );
    }
}
void TcpServer::bindSocket() {

    sockaddr_in address{};
    //ipv4的地址结构，一般表示address+port,这里是一个结构体

    address.sin_family = AF_INET;

    address.sin_port =
        //字节序 host to network short 主机字节序->网络字节序
        htons(port_);

    if (inet_pton(
            AF_INET,
            ip_.c_str(),
            &address.sin_addr
        ) <= 0) {
        //把字符串转化为内核需要的32位二进制的ip
        std::cout<<"绑定socket"<<std::endl;

        throw std::runtime_error(
            "Invalid IP address"
        );
        }

    if (bind(
            server_fd_,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) == -1) {
        //socket和某个ip关联起来


        throw std::runtime_error(
            "Failed to bind socket"
        );
        }
}
void TcpServer::listenSocket() {

    if (listen(server_fd_, 128) == -1) {
        //128表示最多的连接数
        throw std::runtime_error(
            "Failed to listen"
        );
    }

    std::cout
        << "Server listening on "
        << ip_
        << ":"
        << port_
        << std::endl;
}
void TcpServer::acceptLoop() {

    while (true) {

        int client_fd =
            accept(
                server_fd_,
                nullptr,
                nullptr
            );
        //client_fd是另一个文件描述符

        if (client_fd == -1) {
            std::cerr
                << "accept failed"
                << std::endl;

            continue;
        }

        char buffer[1024]{};
        //缓冲区

        ssize_t bytes =
            //接收，从client，放到buffer，大小是sizeof,flag
            recv(
                client_fd,
                buffer,
                sizeof(buffer) - 1,
                0
            );

        if (bytes > 0) {

            std::cout
                << "Received: "
                << buffer
                << std::endl;

            const char* response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 5\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello";


            send(
                client_fd,
                response,
                std::strlen(response),
                0
            );
        }

        close(client_fd);
    }
}
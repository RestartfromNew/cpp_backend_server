#include <iostream>
#include "TcpServer.h"
int main() {
    std::cout << "TcpServer" << std::endl;
    try {
        TcpServer server("0.0.0.0",8080);
        server.start();

    }catch(const std::exception& e) {
        std::cout << "Error starting server: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <sodium.h>
#include <stdexcept>
#include "common/UniqueFd.h"
#include "databases/DatabaseConnection.h"
#include "handler/UserHandler.h"
#include "http/HttpParse.h"
#include "handler/Router.h"
#include "repository/UserRepository.h"
#include "server/Connection.h"
#include "server/TcpServer.h"
#include "service/UserService.h"
#include "http/HttpResponseGenerator.h"
#include <cstdlib>
#include "http/HttpSession.h"
#include "service/RegisterService.h"

namespace {
} // namespace

int main()
{
    try {
        //读取配置
        if (sodium_init() < 0) {
            throw std::runtime_error("Failed to initialize libsodium");
        }
        const char* databaseUrl = std::getenv("DATABASE_URL");
        if (databaseUrl == nullptr || databaseUrl[0] == '\0') {
            throw std::runtime_error("DATABASE_URL environment variable is not set or is empty");
        }
        //组装组件
        DatabaseConnection database{databaseUrl};
        UserRepository userRepository{database};
        UserService userService{userRepository};
        RegisterService registerService{userRepository};
        UserHandler userHandler{userService,registerService};
        Router router;
        //注册路由，如果方法为Get,路径为path,就调用userHanler.getUser方法
        router.addRoute(
            HttpMethod::GET,
            "/user",
            [&userHandler](const HttpRequest& request)->HttpResponse {
                //返回一个HttpResponse
                return userHandler.getUser(request);
            }
        );
        router.addRoute(
            HttpMethod::POST,
            "/register_by_email",
            [&userHandler](const HttpRequest& request)->HttpResponse {
                //返回一个HttpResponse
                return userHandler.Register(request);
            }
            );
        //启动服务
        TcpServer server{"0.0.0.0",8080};
        server.start();
        std::cout
            << "Server listening on port 8080\n";
        while (true) {UniqueFd clientFd =server.acceptConnection();
            if (!clientFd.valid()) {
                continue;
            }
            Connection connection{std::move(clientFd)};
            HttpSession session(std::move(connection),router);
            try {
                session.HandleHttpSession();
            } catch (const std::exception& error) {
                std::cerr
                    << "Connection error: "
                    << error.what()
                    << '\n';

                if (session.is_open()) {
                    try {
                        HttpResponse response=ErrorResponseMaker(HttpStatus::Internal_Server_Error,"Internal Server Error","Internal Server Error");
                        session.sendAll(response);
                    } catch (...) {
                    }
                }
            }
        }
    } catch (const std::exception& error) {
        std::cerr
            << "Server error: "
            << error.what()
            << '\n';
        return 1;
    }
}

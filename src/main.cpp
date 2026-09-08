#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "common/UniqueFd.h"
#include "databases/DatabaseConnection.h"
#include "handler/UserHandler.h"
#include "http/HttpParse.h"
#include "handler/Router.h"
#include "repository/UserRepository.h"
#include "server/Connection.h"
#include "server/TcpServer.h"
#include "service/UserService.h"

namespace {

void sendTextResponse(
    Connection& connection,
    int statusCode,
    const std::string& reasonPhrase,
    const std::string& body
)
{
    const std::string response =
        "HTTP/1.1 " +
        std::to_string(statusCode) +
        " " +
        reasonPhrase +
        "\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    connection.sendAll(response);
}

void handleConnection(
    Connection& connection,
    const Router& router
)
{
    HttpParse parser;

    while (connection.is_open()) {
        const ssize_t bytesRead =
            connection.read();

        if (bytesRead == 0) {
            return;
        }

        if (bytesRead < 0) {
            throw std::runtime_error(
                "Failed to read from client"
            );
        }

        const ParseResult result =
            parser.parse(
                connection.inputBuffer()
            );

        if (result.consumed > 0) {
            connection.consumeInput(
                result.consumed
            );
        }

        if (
            result.status ==
            ParseStatus::NeedMoreData
        ) {
            continue;
        }

        if (
            result.status ==
            ParseStatus::Error
        ) {
            sendTextResponse(
                connection,
                400,
                "Bad Request",
                "Bad Request"
            );

            return;
        }

        if (
            result.status ==
            ParseStatus::Complete
        ) {
            HttpRequest request =
                parser.takeRequest();

            const bool matched =
                router.route(request);

            if (!matched) {
                sendTextResponse(
                    connection,
                    404,
                    "Not Found",
                    "Route not found"
                );

                return;
            }

            sendTextResponse(
                connection,
                200,
                "OK",
                "Request handled successfully"
            );

            return;
        }
    }
}

} // namespace

int main()
{
    try {
        const std::string databaseUrl =
            "host=127.0.0.1 "
            "port=5432 "
            "dbname=?? "
            "user=?? "
            "password=??";

        DatabaseConnection database{
            databaseUrl
        };

        UserRepository userRepository{
            database
        };

        UserService userService{
            userRepository
        };

        UserHandler userHandler{
            userService
        };

        Router router;

        router.addRoute(
            HttpMethod::GET,
            "/user",
            [&userHandler](
                const HttpRequest& request
            ) {
                userHandler.getUser(request);
            }
        );

        TcpServer server{
            "0.0.0.0",
            8080
        };

        server.start();

        std::cout
            << "Server listening on port 8080\n";

        while (true) {
            UniqueFd clientFd =
                server.acceptConnection();

            if (!clientFd.valid()) {
                continue;
            }

            Connection connection{
                std::move(clientFd)
            };

            try {
                handleConnection(
                    connection,
                    router
                );
            } catch (const std::exception& error) {
                std::cerr
                    << "Connection error: "
                    << error.what()
                    << '\n';

                if (connection.is_open()) {
                    try {
                        sendTextResponse(
                            connection,
                            500,
                            "Internal Server Error",
                            "Internal Server Error"
                        );
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

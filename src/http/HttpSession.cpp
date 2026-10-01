//
// Created by yangb on 2026/9/11.
//

#include "http/HttpSession.h"

#include <cctype>

HttpSession::HttpSession(Connection &&connection, Router &router) :connection_(std::move(connection)), router_(router) {
}
std::optional<PendingWebsocket> HttpSession::HandleHttpSession() {
    HttpResponseGenerator generator;
    HttpResponse response;
    while (connection_.is_open()) {
        const ssize_t bytesRead =connection_.read();
        if (bytesRead == 0) {
            return std::nullopt;
        }
        if (bytesRead < 0) {
            throw std::runtime_error("Failed to read from client");
        }

        const ParseResult result =parser_.parse(connection_.inputBuffer());

        if (result.consumed > 0) {
            connection_.consumeInput(result.consumed);
        }

        if (result.status ==ParseStatus::NeedMoreData) {
            continue;
        }
        if (result.status == ParseStatus::Error) {
            response = ErrorResponseMaker(HttpStatus::Bad_Request,"invalid_http_request","Invalid HTTP request");

            response.headers["Connection"] = "close";
            connection_.sendAll(generator.generateHttpResponse(response));

            return std::nullopt;
        }
        if (result.status == ParseStatus::Complete) {
            HttpRequest request = parser_.takeRequest();

            const auto equalsIgnoreCase = [](
                std::string_view left,
                std::string_view right
            ) {
                if (left.size() != right.size()) return false;
                for (std::size_t i = 0; i < left.size(); ++i) {
                    const auto leftCharacter = static_cast<unsigned char>(left[i]);
                    const auto rightCharacter = static_cast<unsigned char>(right[i]);
                    if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
                        return false;
                    }
                }
                return true;
            };

            bool websocketUpgrade = false;
            for (const auto& [name, rawValue] : request.headers) {
                if (!equalsIgnoreCase(name, "Upgrade")) continue;

                std::string_view value = rawValue;
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                    value.remove_prefix(1);
                }
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                    value.remove_suffix(1);
                }

                websocketUpgrade = equalsIgnoreCase(value, "websocket");
                break;
            }

            if (websocketUpgrade) {
                return PendingWebsocket{std::move(connection_),std::move(request)};
            }
            try {
                response = router_.route(request);
                response.headers["Connection"] = "close";
                std::string bytes =generator.generateHttpResponse(response);
                connection_.sendAll(bytes);
                return std::nullopt;
            }
            catch (DatabaseError &error) {
                std::cerr
               << "HTTP request failed: "
               << error.what()
               << '\n';
                switch (error.kind()) {
                    case DatabaseErrorKind::Connection:
                        response = ErrorResponseMaker(
                            HttpStatus::Service_Unavailable,
                            "service_unavailable",
                            "Service unavailable"
                        );
                        break;
                    case DatabaseErrorKind::Query:
                        response = ErrorResponseMaker(
                           HttpStatus::Internal_Server_Error,
                           "internal_server_error",
                           "Internal server error"
                       );
                        break;
                    case DatabaseErrorKind::Constraint:
                    case DatabaseErrorKind::DataConversion:
                        response = ErrorResponseMaker(
                            HttpStatus::Internal_Server_Error,
                            "internal_server_error",
                            "Internal Server Error"
                        );
                        break;
                }

            }
            catch (const std::exception& error) {
                std::cerr
               << "HTTP request failed: "
               << error.what()
               << '\n';
                // 内部记录 error.what()。
                response = ErrorResponseMaker(
                    HttpStatus::Internal_Server_Error,
                    "internal_server_error",
                    "Internal server error"
                );
            }
            response.headers["Connection"] = "close";
            connection_.sendAll(generator.generateHttpResponse(response));
            return std::nullopt;

        }
    }
    return std::nullopt;
}

bool HttpSession::is_open() {
    return connection_.is_open();
};

void HttpSession::sendAll(HttpResponse &response) {
    HttpResponseGenerator generator;
    const std::string responseString = generator.generateHttpResponse(response);
    connection_.sendAll(responseString);
}

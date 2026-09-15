//
// Created by yangb on 2026/9/11.
//

#include "http/HttpSession.h"

HttpSession::HttpSession(Connection &&connection, Router &router) :connection_(std::move(connection)), router_(router) {
}
void HttpSession::HandleHttpSession() {
    HttpResponseGenerator generator;
    HttpResponse response;
    while (connection_.is_open()) {
        const ssize_t bytesRead =connection_.read();
        if (bytesRead == 0) {
            return;
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

            return;
        }
        if (result.status == ParseStatus::Complete) {
            HttpRequest request = parser_.takeRequest();
            try {
                response = router_.route(request);
                response.headers["Connection"] = "close";
                std::string bytes =generator.generateHttpResponse(response);
                connection_.sendAll(bytes);
                return;
            }
            catch (DatabaseError &error) {
                switch (error.kind()) {
                    case DatabaseErrorKind::Connection:
                        response = ErrorResponseMaker(
                            HttpStatus::Service_Unavailable,
                            "service_unavailable",
                            "Service unavailable"
                        );
                        break;

                    case DatabaseErrorKind::Query:
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
                // 内部记录 error.what()。
                response = ErrorResponseMaker(
                    HttpStatus::Internal_Server_Error,
                    "internal_server_error",
                    "Internal server error"
                );
            }
            response.headers["Connection"] = "close";
            connection_.sendAll(generator.generateHttpResponse(response));
            return;

        }
    }
}

bool HttpSession::is_open() {
    return connection_.is_open();
};

void HttpSession::sendAll(HttpResponse &response) {
    HttpResponseGenerator generator;
    const std::string responseString = generator.generateHttpResponse(response);
    connection_.sendAll(responseString);
}


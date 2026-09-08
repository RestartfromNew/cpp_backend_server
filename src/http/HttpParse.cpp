//
// Created by yangb on 2026/9/8.
//

#include "http/HttpParse.h"
#include <charconv>
#include <system_error>

ParseResult HttpParse::parse(std::string_view buffer) {
    std::size_t consumed = 0;

    while (true) {
        std::string_view sub_buffer = buffer.substr(consumed);

        switch (state_) {

            case State::RequestLine: {
                StepResult result = parseRequestLine(sub_buffer);

                if (result.status == StepStatus::NeedMoreData) {
                    return {ParseStatus::NeedMoreData, consumed};
                }

                if (result.status == StepStatus::Error) {
                    state_ = State::Error;
                    return {ParseStatus::Error, consumed};
                }

                consumed += result.consumed;
                state_ = State::Header;
                break;
            }

            case State::Header: {
                StepResult result = parseHeader(sub_buffer);

                if (result.status == StepStatus::NeedMoreData) {
                    return {ParseStatus::NeedMoreData, consumed};
                }

                if (result.status == StepStatus::Error) {
                    state_ = State::Error;
                    return {ParseStatus::Error, consumed};
                }

                consumed += result.consumed;

                // 只有读到空行时，Header 才真正结束。
                if (result.headersFinished) {
                    if (content_length == 0) {
                        // 没有 Body，整个请求完成。
                        state_ = State::Complete;
                    } else {
                        // 有 Body，继续解析指定长度的数据。
                        state_ = State::Body;
                    }
                }

                // headersFinished == false 时不修改 state_。
                // 它仍然是 State::Header，
                // 外层 while 会继续解析下一行 Header。
                break;
            }

            case State::Body: {
                StepResult result = parseBody(sub_buffer);

                if (result.status == StepStatus::NeedMoreData) {
                    return {ParseStatus::NeedMoreData, consumed};
                }

                if (result.status == StepStatus::Error) {
                    state_ = State::Error;
                    return {ParseStatus::Error, consumed};
                }

                consumed += result.consumed;
                state_ = State::Complete;
                break;
            }

            case State::Complete: {
                return {
                    ParseStatus::Complete,
                    consumed
                };
            }

            case State::Error: {
                return {
                    ParseStatus::Error,
                    consumed
                };
            }
        }
    }
}

HttpRequest HttpParse::takeRequest() {
    //转移所有权
    if (state_ != State::Complete) {
        throw std::logic_error(
            "HTTP request is not complete"
        );
    }

    HttpRequest completed =
        std::move(request);

    reset();

    return completed;
}
StepResult HttpParse::parseRequestLine(std::string_view input) {
    std::size_t end=input.find("\r\n");
    if (end==std::string_view::npos) {
        return StepResult{StepStatus::NeedMoreData,0};
        }
    std::size_t space_method=input.find(" ");
    std::size_t space_version=input.find(" ", space_method + 1);
    if (space_method==std::string_view::npos||space_version==std::string_view::npos) {
        return  StepResult{StepStatus::Error,0};
    }
    request.method=parseMethod(input.substr(0, space_method));
    request.path=std::string(input.substr(space_method+1,space_version-space_method-1));
    request.version=std::string(input.substr(space_version+1,end-space_version-1));


    return StepResult{StepStatus::Complete,end+2,true};

};
StepResult HttpParse::parseHeader(
    std::string_view input
)
{
    const std::size_t lineEnd =
        input.find("\r\n");

    if (lineEnd == std::string_view::npos) {
        return {
            StepStatus::NeedMoreData,
            0
        };
    }

    // 空行表示所有 Header 已经结束。
    if (lineEnd == 0) {
        return {
            StepStatus::Complete,
            2,
            true
        };
    }

    const std::string_view line =
        input.substr(0, lineEnd);

    const std::size_t separator =
        line.find(':');

    if (separator == std::string_view::npos) {
        return {
            StepStatus::Error,
            0
        };
    }

    const std::string_view name =
        line.substr(0, separator);

    if (name.empty()) {
        return {
            StepStatus::Error,
            0
        };
    }

    std::string_view value =
        line.substr(separator + 1);

    // 暂时只清理 Header value 前面的空格。
    while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }

    request.headers[
        std::string{name}
    ] = std::string{value};

    if (name == "Content-Length") {
        if (!parseContentLength(value)) {
            return {
                StepStatus::Error,
                0
            };
        }
    }

    return {
        StepStatus::Complete,
        lineEnd + 2,
        false
    };
}
StepResult HttpParse::parseBody(std::string_view input) {
    if (input.size() < content_length) {
        return {
            StepStatus::NeedMoreData,
            0
        };
    }

    request.body.assign(
        input.data(),
        content_length
    );

    return {
        StepStatus::Complete,
        content_length
    };
};
void HttpParse::reset() {
    request = HttpRequest{};
    state_ = State::RequestLine;
    content_length = 0;
}

HttpMethod HttpParse::parseMethod(std::string_view method) {
        if (method == "GET") return HttpMethod::GET;
        if (method == "POST") return HttpMethod::POST;
        if (method == "PUT") return HttpMethod::PUT;
        if (method == "DELETE") return HttpMethod::DELETE;
        if (method == "HEAD") return HttpMethod::HEAD;
        if (method == "OPTIONS") return HttpMethod::OPTIONS;
        if (method == "CONNECT") return HttpMethod::CONNECT;
        if (method == "TRACE") return HttpMethod::TRACE;
        if (method == "PATCH") return HttpMethod::PATCH;
        return HttpMethod::UNKNOWN;
    }
bool HttpParse::parseContentLength(
    std::string_view text
)
{
    if (text.empty()) {
        return false;
    }

    std::size_t length = 0;

    const auto [end, error] =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            length
        );

    if (
        error != std::errc{} ||
        end != text.data() + text.size()
    ) {
        return false;
    }

    // 第一版先限制 Body 最大为 1 MB。
    constexpr std::size_t MaxBodySize =
        1024 * 1024;

    if (length > MaxBodySize) {
        return false;
    }

    content_length = length;
    return true;
}

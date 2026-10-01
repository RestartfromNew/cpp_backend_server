#include "auth/AccessTokenService.h"
#include "common/UniqueFd.h"
#include "handler/Router.h"
#include "http/AuthMiddleWare.h"
#include "server/TcpServer.h"
#include "server/ThreadPool.h"
#include "websocket/WebSocketPool.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct DecodedFrame {
    std::uint8_t opcode = 0;
    bool fin = false;
    std::vector<std::uint8_t> payload;
};

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}

const char* opcodeName(std::uint8_t opcode) {
    switch (opcode) {
        case 0x0: return "Continuation";
        case 0x1: return "Text";
        case 0x2: return "Binary";
        case 0x8: return "Close";
        case 0x9: return "Ping";
        case 0xA: return "Pong";
        default: return "Unknown";
    }
}

std::string payloadText(const std::vector<std::uint8_t>& payload) {
    std::string result;
    result.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        if (byte >= 0x20 && byte <= 0x7E) {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('.');
        }
    }
    return result;
}

void logFrame(
    std::string_view direction,
    const DecodedFrame& frame,
    std::chrono::steady_clock::time_point startedAt
) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt
    );
    std::cout
        << '[' << std::setw(6) << elapsed.count() << " ms] "
        << direction
        << " opcode=" << opcodeName(frame.opcode)
        << " fin=" << std::boolalpha << frame.fin
        << " bytes=" << frame.payload.size();
    if (!frame.payload.empty()) {
        std::cout << " payload=\"" << payloadText(frame.payload) << '\"';
    }
    std::cout << '\n';
}

void sendAll(int fd, const std::vector<std::uint8_t>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t sent = ::send(
            fd,
            bytes.data() + offset,
            bytes.size() - offset,
            MSG_NOSIGNAL
        );
        if (sent < 0 && errno == EINTR) continue;
        require(sent > 0, "Failed to send WebSocket bytes");
        offset += static_cast<std::size_t>(sent);
    }
}

void sendAll(int fd, std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t sent = ::send(
            fd,
            bytes.data() + offset,
            bytes.size() - offset,
            MSG_NOSIGNAL
        );
        if (sent < 0 && errno == EINTR) continue;
        require(sent > 0, "Failed to send HTTP handshake");
        offset += static_cast<std::size_t>(sent);
    }
}

std::vector<std::uint8_t> encodeClientFrame(
    std::uint8_t opcode,
    const std::vector<std::uint8_t>& payload
) {
    std::vector<std::uint8_t> bytes;
    bytes.push_back(static_cast<std::uint8_t>(0x80U | opcode));

    const std::uint64_t size = payload.size();
    if (size <= 125U) {
        bytes.push_back(static_cast<std::uint8_t>(0x80U | size));
    } else if (size <= 0xFFFFU) {
        bytes.push_back(0x80U | 126U);
        bytes.push_back(static_cast<std::uint8_t>((size >> 8U) & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(size & 0xFFU));
    } else {
        bytes.push_back(0x80U | 127U);
        for (int shift = 56; shift >= 0; shift -= 8) {
            bytes.push_back(static_cast<std::uint8_t>((size >> shift) & 0xFFU));
        }
    }

    std::random_device random;
    const std::array<std::uint8_t, 4> mask{
        static_cast<std::uint8_t>(random()),
        static_cast<std::uint8_t>(random()),
        static_cast<std::uint8_t>(random()),
        static_cast<std::uint8_t>(random())
    };
    bytes.insert(bytes.end(), mask.begin(), mask.end());

    for (std::size_t i = 0; i < payload.size(); ++i) {
        bytes.push_back(static_cast<std::uint8_t>(payload[i] ^ mask[i % mask.size()]));
    }
    return bytes;
}

void sendFrame(
    int fd,
    std::uint8_t opcode,
    const std::vector<std::uint8_t>& payload,
    std::chrono::steady_clock::time_point startedAt
) {
    sendAll(fd, encodeClientFrame(opcode, payload));
    logFrame("client -> server", DecodedFrame{opcode, true, payload}, startedAt);
}

bool tryDecodeServerFrame(std::vector<std::uint8_t>& input, DecodedFrame& frame) {
    if (input.size() < 2U) return false;

    frame.fin = (input[0] & 0x80U) != 0;
    frame.opcode = static_cast<std::uint8_t>(input[0] & 0x0FU);
    const bool masked = (input[1] & 0x80U) != 0;
    require(!masked, "Server frame must not be masked");

    std::uint64_t payloadSize = input[1] & 0x7FU;
    std::size_t offset = 2;
    if (payloadSize == 126U) {
        if (input.size() < 4U) return false;
        payloadSize = (static_cast<std::uint64_t>(input[2]) << 8U) | input[3];
        offset = 4;
    } else if (payloadSize == 127U) {
        if (input.size() < 10U) return false;
        payloadSize = 0;
        for (std::size_t i = 2; i < 10; ++i) {
            payloadSize = (payloadSize << 8U) | input[i];
        }
        offset = 10;
    }

    require(payloadSize <= 1024U * 1024U, "Server frame exceeds test limit");
    if (input.size() < offset + static_cast<std::size_t>(payloadSize)) return false;

    frame.payload.assign(
        input.begin() + static_cast<std::ptrdiff_t>(offset),
        input.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize)
    );
    input.erase(
        input.begin(),
        input.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize)
    );
    return true;
}

UniqueFd connectClient(int port) {
    UniqueFd fd{::socket(AF_INET, SOCK_STREAM, 0)};
    require(fd.valid(), "Failed to create client socket");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    require(
        ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1,
        "Failed to parse loopback address"
    );
    require(
        ::connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
        "Failed to connect to test server"
    );
    return fd;
}

void performHandshake(int fd, int port) {
    const std::string request =
        "GET /websocket-test HTTP/1.1\r\n"
        "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    sendAll(fd, request);

    std::string response;
    std::array<char, 1024> buffer{};
    while (response.find("\r\n\r\n") == std::string::npos) {
        pollfd descriptor{fd, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 5000);
        if (ready < 0 && errno == EINTR) continue;
        require(ready > 0, "Timed out waiting for WebSocket handshake");
        const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), 0);
        require(received > 0, "Connection closed during WebSocket handshake");
        response.append(buffer.data(), static_cast<std::size_t>(received));
    }

    std::cout << "----- HTTP upgrade response -----\n" << response;
    require(response.find("101 Switching Protocols") != std::string::npos, "Expected HTTP 101 response");
    require(
        response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos,
        "Invalid Sec-WebSocket-Accept value"
    );
    std::cout << "----- WebSocket connected; holding for 60 seconds -----\n";
}

} // namespace

int main() {
    WebSocketPool websocketPool{{2, 32}};
    TcpServer server{"127.0.0.1", 0};
    std::exception_ptr serverFailure;
    std::thread acceptor;

    try {
        AccessTokenService tokens{"websocket-integration-test-secret"};
        AuthMiddleWare auth{tokens};
        Router router{auth};
        ThreadPool threadPool{{2, 16}, server, router, websocketPool};

        server.start();
        require(websocketPool.init(), "Failed to initialize WebSocket pool");
        require(threadPool.init(), "Failed to initialize HTTP thread pool");
        acceptor = std::thread([&] {
            try {
                threadPool.running();
            } catch (...) {
                serverFailure = std::current_exception();
            }
        });

        try {
            UniqueFd client = connectClient(server.localPort());
            performHandshake(client.get(), server.localPort());

            const auto startedAt = std::chrono::steady_clock::now();
            const auto deadline = startedAt + 60s;
            bool clientPingSent = false;
            bool secondTextSent = false;
            std::vector<std::uint8_t> receivedBytes;

            const std::string firstText = "hello from websocket integration test";
            sendFrame(
                client.get(),
                0x1,
                {firstText.begin(), firstText.end()},
                startedAt
            );

            while (std::chrono::steady_clock::now() < deadline) {
                const auto elapsed = std::chrono::steady_clock::now() - startedAt;
                if (!clientPingSent && elapsed >= 45s) {
                    const std::string payload = "client-heartbeat";
                    sendFrame(client.get(), 0x9, {payload.begin(), payload.end()}, startedAt);
                    clientPingSent = true;
                }
                if (!secondTextSent && elapsed >= 50s) {
                    const std::string payload = "connection still alive after 50 seconds";
                    sendFrame(client.get(), 0x1, {payload.begin(), payload.end()}, startedAt);
                    secondTextSent = true;
                }

                pollfd descriptor{client.get(), POLLIN, 0};
                const int ready = ::poll(&descriptor, 1, 250);
                if (ready < 0 && errno == EINTR) continue;
                require(ready >= 0, "poll failed while maintaining WebSocket connection");
                if (ready == 0) continue;
                require((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0, "WebSocket connection failed");

                if ((descriptor.revents & POLLIN) != 0) {
                    std::array<std::uint8_t, 4096> buffer{};
                    const ssize_t count = ::recv(client.get(), buffer.data(), buffer.size(), 0);
                    require(count > 0, "WebSocket server closed the connection early");
                    receivedBytes.insert(receivedBytes.end(), buffer.begin(), buffer.begin() + count);

                    DecodedFrame frame;
                    while (tryDecodeServerFrame(receivedBytes, frame)) {
                        logFrame("server -> client", frame, startedAt);
                        if (frame.opcode == 0x9) {
                            sendFrame(client.get(), 0xA, frame.payload, startedAt);
                            std::cout << "          heartbeat: server Ping answered with client Pong\n";
                        } else if (frame.opcode == 0xA) {
                            std::cout << "          heartbeat: client Ping confirmed by server Pong\n";
                        }
                    }
                }
            }

            sendFrame(client.get(), 0x8, {0x03, 0xE8}, startedAt);
            std::cout << "PASS: WebSocket stayed connected for 60 seconds\n";
        } catch (...) {
            threadPool.requestStop();
            if (acceptor.joinable()) acceptor.join();
            threadPool.close();
            websocketPool.close();
            throw;
        }

        threadPool.requestStop();
        if (acceptor.joinable()) acceptor.join();
        threadPool.close();
        websocketPool.close();
        if (serverFailure) std::rethrow_exception(serverFailure);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (acceptor.joinable()) acceptor.join();
        websocketPool.close();
        return 1;
    }
}

#include "server/ThreadPool.h"
#include "Auth/AccessTokenService.h"
#include "http/AuthMiddleWare.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
UniqueFd connectClient(int port) {
    UniqueFd fd{socket(AF_INET, SOCK_STREAM, 0)};
    require(fd.valid(), "socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    require(connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
            "connect failed");
    timeval timeout{5, 0};
    require(setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
            "timeout setup failed");
    return fd;
}
void sendRequest(int fd, const std::string& request) {
    std::size_t offset = 0;
    while (offset < request.size()) {
        const auto sent = send(fd, request.data() + offset, request.size() - offset, MSG_NOSIGNAL);
        require(sent > 0, "send failed");
        offset += static_cast<std::size_t>(sent);
    }
}
// Keeps the accepting thread joined even if an assertion throws.
struct RunningPool {
    ThreadPool& pool;
    std::exception_ptr failure;
    std::thread acceptor;
    explicit RunningPool(ThreadPool& value) : pool(value), acceptor([this] {
        try { pool.running(); } catch (...) { failure = std::current_exception(); }
    }) {}
    void stop() {
        pool.requestStop();
        if (acceptor.joinable()) acceptor.join();
        pool.close();
    }
    ~RunningPool() { stop(); }
};
}

int main() {
    try {
        AccessTokenService tokens{"local-test-only-not-a-production-secret"};
        AuthMiddleWare auth{tokens};
        Router router{auth};
        std::mutex mutex;
        std::condition_variable entered;
        std::set<std::thread::id> workers;
        int arrivals = 0;
        bool barrierFailed = false;
        router.addRoute(HttpMethod::GET, "/test/work", [&](const HttpRequest&) {
            std::unique_lock lock(mutex);
            workers.insert(std::this_thread::get_id());
            ++arrivals;
            entered.notify_all();
            // All four handlers must run at once. A single worker cannot fake this test.
            if (!entered.wait_for(lock, 3s, [&] { return arrivals >= 4; }))
                barrierFailed = true;
            HttpResponse response;
            response.status = HttpStatus::Ok;
            response.body = "worker task completed";
            return response;
        });
        {
            TcpServer server{"127.0.0.1", 0};
            server.start();
            ThreadPool pool{{4, 8}, server, router};
            require(pool.init(), "pool init failed");
            require(!pool.init(), "duplicate init must be rejected");
            RunningPool running{pool};
            std::vector<UniqueFd> clients;
            for (int i = 0; i < 4; ++i) {
                clients.push_back(connectClient(server.localPort()));
                sendRequest(clients.back().get(), "GET /test/work HTTP/1.1\r\nHost: localhost\r\n\r\n");
            }
            for (auto& client : clients) {
                std::string response;
                char buffer[1024];
                while (true) {
                    const auto count = recv(client.get(), buffer, sizeof(buffer), 0);
                    require(count >= 0, "response timed out");
                    if (count == 0) break;
                    response.append(buffer, static_cast<std::size_t>(count));
                }
                require(response.find("200 OK") != std::string::npos, "expected HTTP 200");
                require(response.find("worker task completed") != std::string::npos, "missing body");
            }
            running.stop();
            if (running.failure) std::rethrow_exception(running.failure);
            require(!barrierFailed && workers.size() == 4 && arrivals == 4,
                    "four distinct workers did not execute concurrently");
            std::cout << "PASS: four distinct workers handled four concurrent HTTP requests\n";
        }
        // A throwing handler must not terminate the process or strand a worker.
        router.addRoute(HttpMethod::GET, "/test/throw", [](const HttpRequest&) -> HttpResponse {
            throw std::runtime_error("intentional test exception");
        });
        {
            TcpServer server{"127.0.0.1", 0};
            server.start();
            ThreadPool pool{{4, 8}, server, router};
            require(pool.init(), "pool init failed");
            RunningPool running{pool};
            auto client = connectClient(server.localPort());
            sendRequest(client.get(), "GET /test/throw HTTP/1.1\r\nHost: localhost\r\n\r\n");
            std::string response;
            char buffer[1024];
            while (true) {
                const auto count = recv(client.get(), buffer, sizeof(buffer), 0);
                require(count >= 0, "exception response timed out");
                if (count == 0) break;
                response.append(buffer, static_cast<std::size_t>(count));
            }
            require(response.find("500 Internal Server Error") != std::string::npos,
                    "handler exception should produce HTTP 500");
            running.stop();
            if (running.failure) std::rethrow_exception(running.failure);
            std::cout << "PASS: handler exception handled and workers joined\n";
        }
        // Stop with idle workers, then with clients that never finish HTTP headers.
        for (int clientCount : {0, 4, 16}) {
            TcpServer server{"127.0.0.1", 0};
            server.start();
            ThreadPool pool{{4, 8}, server, router};
            require(pool.init(), "pool init failed");
            RunningPool running{pool};
            std::vector<UniqueFd> clients;
            for (int i = 0; i < clientCount; ++i) {
                clients.push_back(connectClient(server.localPort()));
                // Keep the connection open; workers may be blocked in recv.
            }
            std::this_thread::sleep_for(200ms);
            const auto begin = std::chrono::steady_clock::now();
            running.stop();
            require(std::chrono::steady_clock::now() - begin < 2s, "shutdown exceeded 2 seconds");
            if (running.failure) std::rethrow_exception(running.failure);
            require(!pool.is_Running(), "accept loop still running");
            pool.close(); // Repeat-close safety.
            std::cout << "PASS: shutdown with " << clientCount << " idle clients\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

//
// Created by yangb on 2026/9/22.
//
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "databases/DatabasePool.h"
#include "databases/ConnectionLease.h"

void checkQuery(ConnectionLease& lease)
{
    auto result = lease.connection_->execute("SELECT 1");

    if (result.rowCount() != 1 ||
        result.columnCount() != 1 ||
        result.value(0, 0) != "1") {
        throw std::runtime_error("Unexpected SELECT 1 result");
    }
}

// Only test-side statistics are recorded; no private pool state is accessed.
void testConcurrentConnections(const char* url)
{
    using Clock = std::chrono::steady_clock;
    constexpr int workerCount = 8;
    constexpr int rounds = 2;

    DatabasePool pool;
    DatabasePoolConfig config{};
    config.min_connections_ = 2;
    config.max_connections = 6;
    config.acquire_time = 5000;
    config.idle_time = 60000;
    config.check_interval = 100;

    const auto started = Clock::now();
    std::mutex outputMutex;
    auto log = [&](int worker, const std::string& message) {
        std::lock_guard<std::mutex> guard{outputMutex};
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - started).count();
        std::cout << '[' << std::setw(5) << elapsed << " ms] [";
        if (worker == 0) std::cout << "main";
        else std::cout << "worker " << worker;
        std::cout << "] " << message << std::endl;
    };

    log(0, "INIT min=2 max=3 workers=8 rounds=2 acquire_timeout=5000ms");
    if (!pool.init(url, config)) {
        throw std::runtime_error("Concurrent pool initialization failed");
    }

    std::mutex statsMutex;
    int active = 0;
    int peak = 0;
    int completed = 0;
    int failures = 0;
    std::set<std::string> activeSessions;
    std::set<std::string> allSessions;

    // All workers wait on one start signal, instead of guessing startup timing.
    std::promise<void> startSignal;
    const auto start = startSignal.get_future().share();
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);

    auto workerBody = [&](int worker) {
        start.wait();
        for (int round = 1; round <= rounds; ++round) {
            const std::string label = "round=" + std::to_string(round) + " ";
            std::string session;
            bool registered = false;
            bool leased = false;
            try {
                log(worker, label + "REQUEST connection (may wait or create)");
                const auto requested = Clock::now();
                {
                    ConnectionLease lease{pool};
                    leased = true;
                    // Destroyed before lease, including during exception unwinding.
                    struct UsageGuard {
                        std::mutex& mutex;
                        std::set<std::string>& sessions;
                        int& active;
                        std::string& session;
                        bool& registered;
                        ~UsageGuard() {
                            if (registered) {
                                std::lock_guard<std::mutex> guard{mutex};
                                sessions.erase(session);
                                --active;
                                registered = false;
                            }
                        }
                    } usage{statsMutex, activeSessions, active, session, registered};
                    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                        Clock::now() - requested).count();
                    auto identity = lease.connection_->execute("SELECT pg_backend_pid()");
                    session = std::string{identity.value(0, 0)};

                    {
                        std::lock_guard<std::mutex> guard{statsMutex};
                        if (!activeSessions.insert(session).second) {
                            throw std::runtime_error("Same database session borrowed concurrently");
                        }
                        registered = true;
                        ++active;
                        peak = std::max(peak, active);
                        allSessions.insert(session);
                    }

                    log(worker, label + "ACQUIRED session=" + session
                        + " acquire_elapsed=" + std::to_string(waited) + "ms");
                    log(worker, label + "QUERY SELECT 1, pg_sleep(0.5): hold connection for ~500ms");
                    auto result = lease.connection_->execute("SELECT 1, pg_sleep(0.5)");
                    if (result.rowCount() != 1 || result.value(0, 0) != "1") {
                        throw std::runtime_error("Unexpected concurrent query result");
                    }
                    log(worker, label + "QUERY OK; RETURNING session=" + session);

                    // Stop tracking use BEFORE Lease destruction permits another borrower.
                    {
                        std::lock_guard<std::mutex> guard{statsMutex};
                        activeSessions.erase(session);
                        --active;
                        registered = false;
                    }
                } // Lease destruction returns the connection here.
                {
                    std::lock_guard<std::mutex> guard{statsMutex};
                    ++completed;
                }
                log(worker, label + "RETURNED session=" + session);
            } catch (const std::exception& error) {
                {
                    std::lock_guard<std::mutex> guard{statsMutex};
                    if (registered) {
                        activeSessions.erase(session);
                        --active;
                    }
                    ++failures;
                }
                log(worker, label + "FAIL: " + error.what()
                    + (leased ? " (Lease scope exited)" : " (acquisition failed)"));
                return;
            } catch (...) {
                {
                    std::lock_guard<std::mutex> guard{statsMutex};
                    if (registered) {
                        activeSessions.erase(session);
                        --active;
                    }
                    ++failures;
                }
                log(worker, label + "FAIL: unknown exception");
                return;
            }
        }
    };

    try {
        for (int worker = 1; worker <= workerCount; ++worker) {
            workers.emplace_back(workerBody, worker);
        }
    } catch (...) {
        // Release already-started workers before jthread destructors join them.
        startSignal.set_value();
        for (auto& worker : workers) worker.join();
        throw;
    }

    log(0, "START: releasing all 8 workers together");
    startSignal.set_value();
    for (auto& worker : workers) worker.join();

    std::ostringstream summary;
    summary << "SUMMARY completed=" << completed << '/' << workerCount * rounds
            << " failures=" << failures << " peak_observed_in_use=" << peak
            << " distinct_sessions=" << allSessions.size() << " sessions=";
    for (const auto& session : allSessions) summary << session << ' ';
    log(0, summary.str());
    log(0, "CLOSE begin: all worker threads joined, all leases returned");
    pool.close();
    log(0, "CLOSE complete");

    if (failures != 0 || completed != workerCount * rounds || active != 0 ||
        !activeSessions.empty() || peak > config.max_connections ||
        allSessions.size() > static_cast<std::size_t>(config.max_connections)) {
        throw std::runtime_error("Concurrent pool assertions failed");
    }
    if (allSessions.size() <= static_cast<std::size_t>(config.min_connections_)) {
        throw std::runtime_error("Expansion beyond minimum was not observed");
    }
    log(0, "[PASS] Concurrent queries, expansion, reuse, capacity bound and shutdown");
}

int main()
{
    try {
        const char* url = std::getenv("DATABASE_URL");

        if (url == nullptr || url[0] == '\0') {
            throw std::runtime_error("DATABASE_URL is not set");
        }

        DatabasePool pool;

        // 只允许一个连接，方便验证归还和超时
        DatabasePoolConfig config{};
        config.min_connections_ = 1;
        config.max_connections = 1;
        config.acquire_time = 300;    // 毫秒
        config.idle_time = 60'000;    // 毫秒
        config.check_interval = 100;  // 毫秒

        if (!pool.init(url, config)) {
            throw std::runtime_error("Pool initialization failed");
        }

        // 测试 1：离开作用域后可以再次获取
        {
            ConnectionLease lease{pool};
            checkQuery(lease);
        }

        {
            ConnectionLease lease{pool};
            checkQuery(lease);
        }

        std::cout << "[PASS] Normal acquire and return\n";

        // 测试 2：异常离开作用域后，Lease 自动归还
        struct SimulatedBusinessError {};

        try {
            ConnectionLease lease{pool};
            checkQuery(lease);
            throw SimulatedBusinessError{};
        } catch (const SimulatedBusinessError&) {
            // 只捕获主动制造的业务异常
        }

        {
            ConnectionLease lease{pool};
            checkQuery(lease);
        }

        std::cout << "[PASS] Return during exception unwinding\n";

        // 测试 3：唯一连接被占用时，第二次获取应该超时
        {
            ConnectionLease held{pool};

            bool timedOut = false;

            try {
                ConnectionLease second{pool};
            } catch (const std::runtime_error& error) {
                // 临时根据你当前的错误信息区分超时
                if (std::string_view{error.what()} !=
                    "Timeout acquiring connection") {
                    throw;
                }

                timedOut = true;
            }

            if (!timedOut) {
                throw std::runtime_error(
                    "Expected acquisition timeout"
                );
            }
        } // held 在这里归还

        std::cout << "[PASS] Acquisition timeout\n";

        // 所有 Lease 都已离开作用域，才调用阻塞式 close()
        pool.close();

        std::cout << "[PASS] Pool shutdown\n";
        testConcurrentConnections(url);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}

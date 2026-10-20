#include "common/Logger.h"
#include <cerrno>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace backend::logging;
int main() {
    errno = EBUSY;
    Logger::write(Level::Debug, "test", "debug");
    Logger::write(Level::Info, "test", "info");
    Logger::write(Level::Warn, "test", "warn");
    Logger::write(Level::Error, "test", "error");
    Logger::write(Level::Off, "test", "off");
    Logger::write(Level::Info, "test", "fields", {
        {"count", 12}, {"fd", -1}, {"session_id", std::uint64_t{18446744073709551615ULL}},
        {"success", true}, {"reason", "quoted\"\nline\r\\end"},
        {"password", "never-print-password"}, {"authorization", "never-print-token"},
        {"packet", "never-print-packet"}, {"login_ticket", "never-print-ticket"}});
    if (errno != EBUSY) return 1;
    { Scope scope{"test", "normal"}; }
    try {
        Scope scope{"test", "throwing"};
        throw std::runtime_error("never-print-exception");
    } catch (const std::exception&) {}
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 8; ++worker) {
        threads.emplace_back([worker] {
            for (int i = 0; i < 40; ++i)
                Logger::write(Level::Info, "test", "parallel", {{"worker_id", worker}, {"count", i}});
        });
    }
    for (auto& thread : threads) thread.join();
    const std::string large(1000, 'x');
    Logger::write(Level::Info, "test", "bounded", {{"reason", std::string_view{large}}});
    // A broken output stream must not replace the business exception/return.
    const auto previous = std::cerr.exceptions();
    std::cerr.setstate(std::ios::badbit);
    try { std::cerr.exceptions(std::ios::badbit); } catch (...) {}
    Logger::write(Level::Error, "test", "sink_failure");
    try { throw std::runtime_error("business-error"); }
    catch (...) {
        Logger::write(Level::Error, "test", "while_handling_error");
        if (!std::current_exception()) return 2;
    }
    std::cerr.exceptions(previous);
    std::cerr.clear();
    return 0;
}

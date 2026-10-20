#pragma once

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <syncstream>
#include <type_traits>
#include <thread>
#include <variant>

// Header-only so existing executables/tests need no new link dependency.
// Only pass fixed component/event names and explicitly selected metadata.
namespace backend::logging {
enum class Level { Debug, Info, Warn, Error, Off };

// Logging must preserve syscall diagnostics used by the caller.
class PreserveErrno {
public:
    PreserveErrno() noexcept : saved_(errno) {}
    ~PreserveErrno() noexcept { errno = saved_; }
private:
    int saved_;
};

struct Field {
    std::string_view name;
    std::variant<std::string_view, std::int64_t, std::uint64_t, bool> value;
    Field(std::string_view key, std::string_view text) noexcept : name(key), value(text) {}
    Field(std::string_view key, const char* text) noexcept : Field(key, std::string_view{text ? text : ""}) {}
    Field(std::string_view key, bool flag) noexcept : name(key), value(flag) {}
    template<class Integer, std::enable_if_t<std::is_integral_v<Integer> &&
        !std::is_same_v<Integer, bool>, int> = 0>
    Field(std::string_view key, Integer number) noexcept : name(key) {
        if constexpr (std::is_signed_v<Integer>) value = static_cast<std::int64_t>(number);
        else value = static_cast<std::uint64_t>(number);
    }
};

class Logger {
public:
    // LOG_LEVEL is read once on first use; restart the process to change it.
    static Level threshold() noexcept {
        static const Level configured = []() noexcept {
            const char* raw = std::getenv("LOG_LEVEL");
            const std::string_view value = raw ? raw : "INFO";
            if (value == "DEBUG" || value == "debug") return Level::Debug;
            if (value == "WARN" || value == "warn") return Level::Warn;
            if (value == "ERROR" || value == "error") return Level::Error;
            if (value == "OFF" || value == "off") return Level::Off;
            return Level::Info; // INFO and unrecognized values use the safe default.
        }();
        return configured;
    }
    static bool enabled(Level level) noexcept {
        return level != Level::Off && threshold() != Level::Off && level >= threshold();
    }
    static const char* name(Level level) noexcept {
        switch (level) {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warn: return "WARN";
            case Level::Error: return "ERROR";
            case Level::Off: return "OFF";
        }
        return "INFO";
    }
    // Best effort: never changes a return value, throws, or retries business work.
    // String fields are borrowed only until this call returns, not retained.
    static void write(Level level, std::string_view component, std::string_view event,
                      std::initializer_list<Field> fields = {}) noexcept {
        PreserveErrno preserveErrno;
        if (!enabled(level)) return;
        try {
            const auto now = std::chrono::system_clock::now();
            const auto seconds = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
#ifdef _WIN32
            if (gmtime_s(&utc, &seconds) != 0) return;
#else
            if (gmtime_r(&seconds, &utc) == nullptr) return;
#endif
            const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;
            std::ostringstream line;
            line << "{\"timestamp\":\"" << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
                 << '.' << std::setfill('0') << std::setw(3) << millis << "Z\",\"level\":";
            quote(line, name(level));
            line << ",\"thread_id\":\"" << std::this_thread::get_id() << '"';
            line << ",\"component\":"; quote(line, component);
            line << ",\"event\":"; quote(line, event);
            line << ",\"fields\":{";
            bool first = true;
            for (const auto& field : fields) {
                if (!first) line << ',';
                first = false;
                quote(line, field.name); line << ':';
                // Allowlisted keys prevent accidental credential/payload fields.
                // This is defense in depth, not a sanitizer for arbitrary text.
                if (!allowed(field.name)) { quote(line, "[REDACTED]"); continue; }
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::string_view>) quote(line, value);
                    else if constexpr (std::is_same_v<T, bool>) line << (value ? "true" : "false");
                    else line << value;
                }, field.value);
            }
            line << "}}\n";
            // Atomic relative to other Logger writes. Legacy cout/cerr is separate.
            std::osyncstream(std::cerr) << line.str();
        } catch (...) {
            // Allocation/formatting/output failure must not mask business errors.
        }
    }
private:
    static bool allowed(std::string_view key) noexcept {
        constexpr std::string_view keys[] = {"operation", "operation_id", "elapsed_us", "outcome",
            "status", "code", "kind", "worker_id", "session_id", "fd", "count", "rows",
            "bytes", "attempt", "opcode", "port", "success", "reason", "pending", "method"};
        for (const auto entry : keys) if (key == entry) return true;
        return false;
    }
    static void quote(std::ostream& out, std::string_view text) {
        constexpr char hex[] = "0123456789abcdef";
        out << '"';
        const auto limit = text.size() > 192 ? 192 : text.size();
        for (std::size_t i = 0; i < limit; ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
            else if (c < 32 || c >= 127) out << "\\u00" << hex[c >> 4] << hex[c & 15];
            else out << static_cast<char>(c);
        }
        if (limit < text.size()) out << "...";
        out << '"';
    }
};

// DEBUG-only timing of one call. 'returned' includes business rejection; it does
// not mean commit/delivery. Exceptions escaping the scope emit ERROR instead.
// Use static/literal names: this guard borrows component/operation until exit.
class Scope {
public:
    Scope(std::string_view component, std::string_view operation) noexcept
        : component_(component), operation_(operation), exceptions_(std::uncaught_exceptions()),
          trace_(false) {
        PreserveErrno preserveErrno;
        trace_ = Logger::enabled(Level::Debug);
        if (trace_) {
            id_ = nextId_.fetch_add(1, std::memory_order_relaxed);
            started_ = std::chrono::steady_clock::now();
            Logger::write(Level::Debug, component_, "scope_begin",
                {{"operation", operation_}, {"operation_id", id_}});
        }
    }
    ~Scope() noexcept {
        PreserveErrno preserveErrno;
        const bool failed = std::uncaught_exceptions() > exceptions_;
        if (!trace_ && !failed) return;
        if (!trace_) {
            Logger::write(Level::Error, component_, "scope_exception", {{"operation", operation_}});
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_).count();
        Logger::write(failed ? Level::Error : Level::Debug, component_, "scope_end",
            {{"operation", operation_}, {"operation_id", id_}, {"elapsed_us", elapsed},
             {"outcome", failed ? "exception" : "returned"}});
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    std::string_view component_, operation_;
    int exceptions_;
    bool trace_;
    std::uint64_t id_ = 0;
    std::chrono::steady_clock::time_point started_{};
    inline static std::atomic<std::uint64_t> nextId_{1};
};
} // namespace backend::logging

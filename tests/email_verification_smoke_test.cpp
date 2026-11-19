#include "service/EmailVerificationService.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabaseError.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

// 手动运行会真实申请验证码并发信，沿用数据库的一分钟限流。
// 配置从环境读取，测试日志仅报告状态。
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    const char* stage = "environment";
    try {
        constexpr const char* recipient = "cindyleblla@126.com";
        constexpr const char* required[] = {"DATABASE_URL", "EMAIL_CODE_HMAC_KEY", "SMTP_URL",
            "SMTP_USERNAME", "SMTP_PASSWORD", "SMTP_FROM", "SMTP_FROM_NAME"};
        bool missing = false;
        for (const char* name : required) {
            const char* value = std::getenv(name);
            if (value == nullptr || *value == '\0') {
                std::cerr << "[FAIL] Missing or empty variable: " << name << '\n';
                missing = true;
            }
        }
        if (missing) return 1;
        std::cout << "[PASS] Required configuration is present\n";
        std::cout << "[test] Recipient: " << recipient << '\n';

        stage = "library initialization";
        if (sodium_init() < 0) throw std::runtime_error("libsodium init failed");
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("libcurl init failed");
        struct CurlCleanup { ~CurlCleanup() { curl_global_cleanup(); } } curl_cleanup;

        stage = "database pool";
        DatabasePool pool;
        const DatabasePoolConfig config{1, 2, 5000, 60000, 1000};
        if (!pool.init(std::getenv("DATABASE_URL"), config)) throw std::runtime_error("Database pool init failed");
        UserRepository repository{pool};

        stage = "mail service initialization";
        EmailVerificationService service{repository};
        std::cout << "[PASS] Database and mail service initialized\n";

        stage = "code creation and SMTP";
        std::cout << "[test] Calling service; SMTP timeout is 30 seconds\n";
        const auto started = std::chrono::steady_clock::now();
        const auto result = service.getVerificationEmailCode(recipient);
        std::cout << "[test] Service elapsed "
            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
            << " ms\n";
        switch (result) {
            case GetVerificationEmailCodeResponse::EmailRegistered:
                std::cerr << "[BLOCKED] Email already registered; no message submitted\n";
                return 2;
            case GetVerificationEmailCodeResponse::ToMuchRequest:
                std::cerr << "[BLOCKED] One-minute limit; wait 60 seconds and rerun\n";
                return 3;
            case GetVerificationEmailCodeResponse::UnknowError:
                std::cerr << "[FAIL] SMTP submission failed; inspect component logs\n"
                    << "[test] The stored request still counts toward the one-minute limit\n";
                return 1;
            case GetVerificationEmailCodeResponse::Send: break;
            default: throw std::runtime_error("Unexpected service response");
        }
        std::cout << "[PASS] SMTP server accepted submission\n";

        stage = "stored verification record";
        ConnectionLease lease{pool};
        const std::string parameters[] = {recipient};
        const auto record = lease.connection().execute(R"(
            SELECT octet_length(code_digest) = 32, expires_at > clock_timestamp(),
                   consumed_at IS NULL, attempt_count = 0
            FROM app.email_verification_codes
            WHERE email = $1::text AND purpose = 'register'
            ORDER BY created_at DESC, id DESC LIMIT 1
        )", parameters);
        if (record.rowCount() != 1) throw std::runtime_error("Missing verification row");
        for (std::size_t column = 0; column < 4; ++column) {
            if (record.value(0, column) != "t") throw std::runtime_error("Invalid verification row");
        }
        std::cout << "[PASS] Stored digest length, expiry, unused state and attempt count\n"
            << "[test] Check Gmail inbox/spam and Resend Emails for delivery\n"
            << "[test] Inbox delivery and subsequent code verification require separate checks\n";
        return 0;
    } catch (const DatabaseError& error) {
        std::cerr << "[FAIL] Stage: " << stage << "; SQLSTATE=" << error.code() << '\n';
    } catch (const std::exception&) {
        // 异常可能包含敏感配置；组件日志提供具体诊断。
        std::cerr << "[FAIL] Stage: " << stage << "; inspect component logs\n";
    }
    return 1;
}

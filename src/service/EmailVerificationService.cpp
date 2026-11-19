//
// Created by yangb on 2026/11/18.
//

#include "service/EmailVerificationService.h"
#include "common/Logger.h"

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

EmailVerificationService::EmailVerificationService(UserRepository & user_repository):user_repository_(user_repository) {
    const auto required = [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            // 仅记录配置项名称，凭据内容不会写入日志。
            backend::logging::Logger::write(backend::logging::Level::Error, "EmailVerificationService", "smtp_config_failed",
                {{"operation", name}, {"reason", value == nullptr ? "missing_environment_variable" : "empty_environment_variable"}});
            throw std::runtime_error(std::string{name} + " environment variable is not set or is empty");
        }
        return std::string{value};
    };
    EMAIL_CODE_HMAC_KEY=required("EMAIL_CODE_HMAC_KEY");
    std::size_t decoded_bytes = 0;
    if (EMAIL_CODE_HMAC_KEY.size() != code_hmac_key_.size() * 2 ||
        sodium_hex2bin(
            code_hmac_key_.data(),
            code_hmac_key_.size(),
            EMAIL_CODE_HMAC_KEY.data(),
            EMAIL_CODE_HMAC_KEY.size(),
            nullptr,
            &decoded_bytes,
            nullptr
        ) != 0 ||
        decoded_bytes != code_hmac_key_.size()) {

        throw std::runtime_error(
            "EMAIL_CODE_HMAC_KEY must contain exactly 64 hexadecimal characters"
        );
        }
    SMTP_URL = required("SMTP_URL");
    SMTP_USERNAME = required("SMTP_USERNAME");
    SMTP_PASSWORD = required("SMTP_PASSWORD");
    SMTP_FROM = required("SMTP_FROM");
    SMTP_FROM_NAME = required("SMTP_FROM_NAME");

    // 此事件表示环境配置加载成功，SMTP 连接和发送结果由后续发信操作确认。
    backend::logging::Logger::write(backend::logging::Level::Info, "EmailVerificationService", "smtp_config_loaded");
}
GetVerificationEmailCodeResponse EmailVerificationService::getVerificationEmailCode(const std::string & email) {

    std::string code=generateCode();
    std::string digest=makeCodeDigest(email,code,code_hmac_key_);
    const auto result =user_repository_.createEmailCodeRecord(email, digest);
    if (result != GetVerificationEmailCodeResponse::Send) {
        return result;
    }
    const std::string message =
    "From: <" + SMTP_FROM + ">\r\n"
    "To: <" + email + ">\r\n"
    "Subject: Email verification code\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: text/plain; charset=UTF-8\r\n"
    "\r\n"
    "Dear Chat User:\r\n"
    "Your verification code is " + code + ".\r\n"
    "It expires in 5 minutes.\r\n";
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        backend::logging::Logger::write(backend::logging::Level::Error, "EmailVerificationService", "smtp_handle_init_failed");
        return GetVerificationEmailCodeResponse::UnknowError;
    }

    MailPayload payload{message};
    curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, email.c_str());
    if (recipients == nullptr) {
        curl_easy_cleanup(curl);
        backend::logging::Logger::write(backend::logging::Level::Error, "EmailVerificationService", "smtp_recipient_init_failed");
        return GetVerificationEmailCodeResponse::UnknowError;
    }

    curl_easy_setopt(curl, CURLOPT_URL, SMTP_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, SMTP_USERNAME.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, SMTP_PASSWORD.c_str());

    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, SMTP_FROM.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readMail);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode sent = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(recipients);

    if (sent != CURLE_OK) {
        backend::logging::Logger::write(backend::logging::Level::Error, "EmailVerificationService", "smtp_send_failed",
            {{"code", static_cast<int>(sent)}});
        return GetVerificationEmailCodeResponse::UnknowError;
    }
    backend::logging::Logger::write(backend::logging::Level::Info, "EmailVerificationService", "smtp_submission_accepted");
    return GetVerificationEmailCodeResponse::Send;

}
std::string EmailVerificationService::generateCode() {
    const auto number = randombytes_uniform(1000000);
    std::string code = std::to_string(number);
    code.insert(0, 6 - code.size(), '0');
    return code;
}
std::string EmailVerificationService::makeCodeDigest(const std::string& email,const std::string& code,const std::array<unsigned char, crypto_auth_hmacsha256_KEYBYTES>& key) {
    const std::string message = "register\n" + email + "\n" + code;
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> digest{};
    crypto_auth_hmacsha256(
        digest.data(),
        reinterpret_cast<const unsigned char*>(message.data()),
        message.size(),
        key.data()
    );
    std::array<char, crypto_auth_hmacsha256_BYTES * 2 + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return std::string{hex.data()};
}
std::size_t EmailVerificationService::readMail(char* buffer, std::size_t size, std::size_t count, void* context) {
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return CURL_READFUNC_ABORT;
    auto& payload = *static_cast<MailPayload*>(context);
    const auto bytes = std::min(size * count, payload.remaining.size());
    if (bytes != 0) {
        std::memcpy(buffer, payload.remaining.data(), bytes);
        payload.remaining.remove_prefix(bytes);
    }
    return bytes;
}
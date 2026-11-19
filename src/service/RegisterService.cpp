#include "common/Logger.h"
//
// Created by yangb on 2026/9/14.
//


#include "service/RegisterService.h"

#include <iostream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace {
    const std::string EMAIL_PATTERN = "^[a-zA-Z0-9_+&*-]+(?:\\.[a-zA-Z0-9_+&*-]+)*" \
        "@(?:[a-zA-Z0-9-]+\\.)+[a-zA-Z]{2,10}$";
    // 用户名使用小写 ASCII，符号可以出现在任意位置。
    const std::string USERNAME_PATTERN="^[a-z0-9_.-]{3,30}$";

}
RegisterService::RegisterService(UserRepository& repository):userRepository_{repository} {
    const char* raw_key = std::getenv("EMAIL_CODE_HMAC_KEY");
    std::size_t decoded = 0;
    if (raw_key == nullptr || std::char_traits<char>::length(raw_key) != code_hmac_key_.size() * 2 ||
        sodium_hex2bin(code_hmac_key_.data(), code_hmac_key_.size(), raw_key,
            code_hmac_key_.size() * 2, nullptr, &decoded, nullptr) != 0 || decoded != code_hmac_key_.size()) {
        backend::logging::Logger::write(backend::logging::Level::Error, "RegisterService", "email_code_key_invalid");
        throw std::runtime_error("EMAIL_CODE_HMAC_KEY must contain exactly 64 hexadecimal characters");
    }
}
//默认再handler中已经进行过去掉前后空格，email已经全部转换为小写
RegisterResult RegisterService::RegisterUserByEmail( const std::string &email,const std::string & password,  const std::string  &display_name,const std::string &username,const std::string &code) {
    backend::logging::Scope operationLog{"RegisterService", "RegisterUserByEmail"};
    if (!checkEmail(email))
        return RegisterResult{RegisterError::InvalidEmail};
    if (!checkPassword(password))
        return RegisterResult{RegisterError::InvalidPassword};
    if (!checkDisplayName(display_name))
        return RegisterResult{RegisterError::InvalidDisplayName};
    auto normalized_username = username;
    std::transform(normalized_username.begin(), normalized_username.end(), normalized_username.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
    });
    if (!checkUsername(normalized_username))
        return RegisterResult{RegisterError::InvalidUsername};
    try {
        const auto verified = verifyCode(email, code);
        switch (verified.status) {
            case VerifyEmailCodeResponse::CodeDoNotMatch: return RegisterResult{RegisterError::CodeDoNotMatch};
            case VerifyEmailCodeResponse::ToMuchTry: return RegisterResult{RegisterError::ToMuchTry};
            case VerifyEmailCodeResponse::CodeExpired: return RegisterResult{RegisterError::CodeExpired};
            case VerifyEmailCodeResponse::Verified: break;
        }
        const auto password_hash = passwordHash(password);
        auto created = userRepository_.CreateNewUserByEmail(email, password_hash, display_name, normalized_username, verified.record_id);
        if (const auto* user = std::get_if<User>(&created)) return RegisterResult{*user};
        // 预验证后验证码可能被另一个注册消费或过期，映射事务内的再次检查结果。
        switch (std::get<VerifyEmailCodeResponse>(created)) {
            case VerifyEmailCodeResponse::CodeDoNotMatch: return RegisterResult{RegisterError::CodeDoNotMatch};
            case VerifyEmailCodeResponse::ToMuchTry: return RegisterResult{RegisterError::ToMuchTry};
            case VerifyEmailCodeResponse::CodeExpired: return RegisterResult{RegisterError::CodeExpired};
            case VerifyEmailCodeResponse::Verified: return RegisterResult{RegisterError::RegisterFailed};
        }
        return RegisterResult{RegisterError::RegisterFailed};
    }
    catch (const DatabaseError &error) {
        if (error.kind()==DatabaseErrorKind::Query&&error.code() == "23505" &&error.constraint() =="password_credentials_login_email_unique") {
            return RegisterResult{RegisterError::EmailExists};
        }
        if (error.kind()==DatabaseErrorKind::Query&&error.code() == "23505" &&error.constraint() =="users_unique") {
            return RegisterResult{RegisterError::UsernameExists};
        }
        if (error.kind() == DatabaseErrorKind::Query &&error.code() == "23514" &&error.constraint() == "users_check") {
            return RegisterResult{RegisterError::InvalidUsername};
    }
        throw;
    }
}
bool RegisterService::checkEmail(const std::string &email) {
    backend::logging::Scope operationLog{"RegisterService", "checkEmail"};
    std::regex pattern(EMAIL_PATTERN);
    // Tests whether a regular expression matches the entire target string.
    return std::regex_match(email, pattern);

}
bool RegisterService::checkDisplayName(const std::string &display_name) {
    backend::logging::Scope operationLog{"RegisterService", "checkDisplayName"};
    // 按 Unicode 码点计数，中文和 emoji 不按 UTF-8 字节数计算。
    // 组合表情可能包含多个码点。NUL 无法存入 PostgreSQL text。
    if (display_name.empty() || display_name.size() > 200) return false;
    std::size_t characters = 0;
    for (std::size_t i = 0; i < display_name.size();) {
        const auto first = static_cast<unsigned char>(display_name[i]);
        std::uint32_t codepoint = 0;
        std::size_t width = 0;
        if (first < 0x80) { codepoint = first; width = 1; }
        else if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; width = 2; }
        else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; width = 3; }
        else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; width = 4; }
        else return false;
        if (width > display_name.size() - i) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto next = static_cast<unsigned char>(display_name[i + j]);
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if (codepoint == 0 || (width == 3 && codepoint < 0x800) || (width == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) return false;
        if (++characters > 50) return false;
        i += width;
    }
    return display_name.find_first_not_of(' ') != std::string::npos;
}
bool RegisterService::checkPassword(const std::string &password) {
    backend::logging::Scope operationLog{"RegisterService", "checkPassword"};
    static const std::regex pattern(R"(^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[^A-Za-z0-9\s])\S{6,20}$)");
    return std::regex_match(password, pattern);

}
bool RegisterService::checkUsername(const std::string &username) {
    backend::logging::Scope operationLog{"RegisterService", "checkUsername"};
    static const std::regex pattern(USERNAME_PATTERN);
    return std::regex_match(username, pattern);
}
std::string RegisterService::passwordHash(const std::string &password) {
    backend::logging::Scope operationLog{"RegisterService", "passwordHash"};
    char hashedPassword[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(
           hashedPassword,
           password.c_str(),
           password.size(),
           crypto_pwhash_OPSLIMIT_INTERACTIVE,
           crypto_pwhash_MEMLIMIT_INTERACTIVE
       ) != 0) {

        throw std::runtime_error("Password hashing failed");
       }

    return std::string(hashedPassword);
}
EmailCodeVerificationResult RegisterService::verifyCode(const std::string& email, const std::string& code) {
    // 与发信 Service 使用同一用途、邮箱和密钥，保持摘要字节完全一致。
    // 非六位数字也进入失败计数，避免格式错误请求绕过尝试次数限制。
    const bool valid_format = code.size() == 6 && code.find_first_not_of("0123456789") == std::string::npos;
    const std::string message = "register\n" + email + "\n" + (valid_format ? code : "invalid-code");
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> digest{};
    crypto_auth_hmacsha256(digest.data(), reinterpret_cast<const unsigned char*>(message.data()), message.size(), code_hmac_key_.data());
    std::array<char, crypto_auth_hmacsha256_BYTES * 2 + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return userRepository_.verifyEmailCode(email, std::string{hex.data()});
}
bool RegisterService::checkEmailInfoAvaliability(const std::string &register_info) {
    return userRepository_.checkEmailAvaliability(register_info);
}
bool  RegisterService::checkUsernameInfoAvaliability(const std::string &register_info) {
    return userRepository_.checkUsernameAvaliability(register_info);
}
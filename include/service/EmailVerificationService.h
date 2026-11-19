//
// Created by yangb on 2026/11/18.
//

#ifndef CPP_BACKEND_SERVER_EMAILVERIFICATIONSERVICE_H
#define CPP_BACKEND_SERVER_EMAILVERIFICATIONSERVICE_H

#include <string>
#include <string_view>
#include <cstddef>
#include "domain/EmailVerifcationResult.h"
#include "repository/UserRepository.h"
#include <sodium.h>
#include <array>
#include <curl/curl.h>
struct MailPayload {
    std::string_view remaining;
};



class EmailVerificationService {
public:
    EmailVerificationService(UserRepository & user_repository);
    GetVerificationEmailCodeResponse getVerificationEmailCode(const std::string & email);



private:
    std::string generateCode();
    std::string makeCodeDigest(const std::string& email,const std::string& code,const std::array<unsigned char, crypto_auth_hmacsha256_KEYBYTES>& key);
    UserRepository user_repository_;
    std::array<unsigned char, crypto_auth_hmacsha256_KEYBYTES> code_hmac_key_;
    std::string EMAIL_CODE_HMAC_KEY;
    std::string SMTP_URL;
    std::string SMTP_USERNAME;
    std::string SMTP_PASSWORD;
    std::string SMTP_FROM;
    std::string SMTP_FROM_NAME;
    // libcurl 通过普通函数指针调用；邮件读取进度由 context 传入。
    static std::size_t readMail(char* buffer, std::size_t size, std::size_t count, void* context);
};


#endif //CPP_BACKEND_SERVER_EMAILVERIFICATIONSERVICE_H

//
// Created by yangb on 2026/9/14.
//


#include "service/RegisterService.h"

#include <iostream>

namespace {
    const std::string EMAIL_PATTERN = "^[a-zA-Z0-9_+&*-]+(?:\\.[a-zA-Z0-9_+&*-]+)*" \
        "@(?:[a-zA-Z0-9-]+\\.)+[a-zA-Z]{2,10}$";
    //3-20个字符，只能包括英文数字字母和下划线
    const std::string DISPLAY_NAME="^[A-Za-z0-9_]{3,20}$";

}
RegisterService::RegisterService(UserRepository& repository):userRepository_{repository} {};
RegisterResult RegisterService::RegisterUserByEmail( std::string email,std::string  password,  std::string  display_name) {
    email.erase(0, email.find_first_not_of(" "));
    email.erase(email.find_last_not_of(" ") + 1);
    display_name.erase(0, display_name.find_first_not_of(" "));
    display_name.erase(display_name.find_last_not_of(" ") + 1);
    password.erase(0, password.find_first_not_of(" "));
    password.erase(password.find_last_not_of(" ") + 1);
    if (!checkEmail(email))
        return RegisterResult{RegisterError::InvalidEmail};
    if (!checkPassword(password))
        return RegisterResult{RegisterError::InvalidPassword};
    if (!checkDisplayName(display_name))
        return RegisterResult{RegisterError::InvalidDisplayName};
    try {
        std::string password_hash=passwordHash(password);
        auto user =userRepository_.CreateNewUserByEmail(email,password_hash,display_name);
        if (user)
            //这是一个装user的容器，要把user取出来
            return RegisterResult{*user};
        else
            return RegisterResult{RegisterError::RegisterFailed};
    }
    catch (const DatabaseError &error) {
        if (error.kind()==DatabaseErrorKind::Query&&error.code() == "23505" &&error.constraint() =="password_credentials_login_email_unique") {
            return RegisterResult{RegisterError::EmailExists};
        }
        throw;
    }
}
bool RegisterService::checkEmail(const std::string &email) {
    std::regex pattern(EMAIL_PATTERN);
    // Tests whether a regular expression matches the entire target string.
    return std::regex_match(email, pattern);

}
bool RegisterService::checkDisplayName(const std::string &display_name) {
    std::regex pattern(DISPLAY_NAME);
    return std::regex_match(display_name, pattern);
    return false;

}
bool RegisterService::checkPassword(const std::string &password) {
    static const std::regex pattern(R"(^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[^A-Za-z0-9\s])\S{6,20}$)");
    return std::regex_match(password, pattern);

}
std::string RegisterService::passwordHash(const std::string &password) {
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
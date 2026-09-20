//
// Created by yangb on 2026/9/8.
//

#include "../../include/handler/UserHandler.h"
#include "handler/UserHandler.h"
#include "nlohmann/json.hpp"

std::string UserHandler::toLower(std::string str) {std::transform(
        str.begin(),
        str.end(),
        str.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return str;
}
std::string UserHandler::conversionInput(const std::string& input) {
    const auto begin = input.find_first_not_of(' ');
    const auto end = input.find_last_not_of(' ');

    const std::string trimmedInput =begin == std::string::npos? std::string{}: input.substr(begin, end - begin + 1);
    return trimmedInput;
}

UserHandler::UserHandler(UserService& userService,RegisterService& registerService,LoginService &loginService): userService_(userService),registerService_(registerService)
,loginService_(loginService){}

HttpResponse UserHandler::Register(const HttpRequest &request) {
    HttpResponse response;
    if (request.body.empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_request_body",
            "Request body is required"
        );
    }
    const nlohmann::json body =nlohmann::json::parse(request.body,nullptr,false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "invalid_json",
            "Request body must be a valid JSON object"
        );
    }
    const auto emailIterator =body.find("email");

    if (emailIterator == body.end() ||!emailIterator->is_string() ||emailIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_email",
            "Email must be a non-empty string"
        );
    }

    const auto passwordIterator =body.find("password");

    if (passwordIterator == body.end() ||!passwordIterator->is_string() ||passwordIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_password",
            "Password must be a non-empty string"
        );
    }

    const auto displayNameIterator =body.find("display_name");
    if (displayNameIterator == body.end() ||!displayNameIterator->is_string() ||displayNameIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_display_name",
            "Display name must be a non-empty string"
        );
    }
    std::string userEmail =emailIterator->get<std::string>();
    std::string userPassword =passwordIterator->get<std::string>();
    std::string userDisplayName =displayNameIterator->get<std::string>();
    userEmail=toLower(conversionInput(userEmail));
    userPassword=conversionInput(userPassword);
    userDisplayName=conversionInput(userDisplayName);
    RegisterResult registerResult= registerService_.RegisterUserByEmail(userEmail,userPassword,userDisplayName);
    auto user=registerResult.result;
    if (std::holds_alternative<User>(user)) {
        const User& user = std::get<User>(registerResult.result);
        nlohmann::json body;
        body["id"] = boost::uuids::to_string(user.id);
        body["display_name"] = user.display_name;
        response.body = body.dump();
        response.status=HttpStatus::Created;
        return response;
    }
    const auto error = std::get<RegisterError>(registerResult.result);
    switch (error) {
        case RegisterError::RegisterFailed:
            return ErrorResponseMaker(
                HttpStatus::Internal_Server_Error,
                "register_failed",
                "Register failed"
            );
        case RegisterError::InvalidEmail:
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_email",
                "Invalid email"
            );

        case RegisterError::InvalidPassword:
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_password",
                "Password does not meet requirements"
            );

        case RegisterError::InvalidDisplayName:
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_display_name",
                "Invalid display name"
            );

        case RegisterError::EmailExists:
            return ErrorResponseMaker(
                HttpStatus::Conflict,
                "email_already_exists",
                "This email is already registered"
            );
    }
    throw std::logic_error("Unhandled RegisterError");
}
HttpResponse UserHandler::LoginByEmail(const HttpRequest& request) {
    HttpResponse response;
    if (request.body.empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_request_body",
            "Request body is required"
        );
    }
    const nlohmann::json body =nlohmann::json::parse(request.body,nullptr,false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "invalid_json",
            "Request body must be a valid JSON object"
        );
    }
    const auto emailIterator =body.find("email");

    if (emailIterator == body.end() ||!emailIterator->is_string() ||emailIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_email",
            "Email must be a non-empty string"
        );
    }

    const auto passwordIterator =body.find("password");

    if (passwordIterator == body.end() ||!passwordIterator->is_string() ||passwordIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_password",
            "Password must be a non-empty string"
        );
    }
    std::string userEmail =emailIterator->get<std::string>();
    std::string userPassword =passwordIterator->get<std::string>();
    userEmail=toLower(conversionInput(userEmail));
    userPassword=conversionInput(userPassword);
    LoginServiceResult loginServiceResult=loginService_.loginByEmail(userEmail,userPassword);
    if (std::holds_alternative<UserToken>(loginServiceResult.login)) {
        const UserToken& user_token = std::get<UserToken>(loginServiceResult.login);
        const User &user=user_token.user;
        const std::string access_token=user_token.access_token;
        const std::string refresh_token=user_token.refresh_token;
        nlohmann::json body;
        body["id"] = boost::uuids::to_string(user.id);
        body["display_name"] = user.display_name;
        body["access_token"] = access_token;
        body["refresh_token"] = refresh_token;
        response.body = body.dump();
        response.status=HttpStatus::Ok;
        return response;
    }
    const auto loginError=std::get<LoginError>(loginServiceResult.login);
    std::cout<<"login_error="<<loginError<<std::endl;
    switch (loginError) {
        case LoginError::NoUserFound:
            return ErrorResponseMaker(HttpStatus::Unauthorized, "invalid_credentials","Email or password is incorrect");
        case LoginError::InvalidCredentials:
            return ErrorResponseMaker(HttpStatus::Unauthorized, "invalid_credentials","Email or password is incorrect");
        case LoginError::NotActiveUser:
            return ErrorResponseMaker(HttpStatus::Forbidden,"account_disabled","Account is disabled");
        case LoginError::InternalError:
            return ErrorResponseMaker(HttpStatus::Internal_Server_Error,"internal_error","Internal error");
        case LoginError::ServiceUnavailable:
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,"service_unavailable","Service unavailable");
    }
    throw std::logic_error("Unhandled LoginError");


}
HttpResponse UserHandler::getUser(const HttpRequest& request)
{
    const auto emailHeader =request.headers.find("X-User-Email");
    HttpResponse response;
    response.headers["Content-Type"] = "application/json";
    // 请求缺少必要参数。
    if (emailHeader == request.headers.end() ||emailHeader->second.empty()) {
       response.status=HttpStatus::Bad_Request;
        response=ErrorResponseMaker(HttpStatus::Bad_Request,"missing_user_email","Missing X-User-Email header");
        return response;
    }
    const std::string& email = emailHeader->second;
    auto user = userService_.findUserByEmail(email);
    // 查询成功，但没有对应用户。
    if (!user) {
       response=ErrorResponseMaker(HttpStatus::Not_Found,"user_not_found","User not found");
        return response;
    }
    // 查询到用户。
    response.status=HttpStatus::Ok;
    nlohmann::json body;
    body["id"] = user->id;
    body["display_name"] = user->display_name;
    response.body = body.dump();
    return response;
};

HttpResponse UserHandler::verifyaccess(const HttpRequest& request) {
    std::cout<<"access_token received"<<std::endl;
    HttpResponse response;
    response.status=HttpStatus::Ok;
    nlohmann::json body;
    body["success"] = "success";
    return response;
}


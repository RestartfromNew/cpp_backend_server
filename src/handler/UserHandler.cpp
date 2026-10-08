//
// Created by yangb on 2026/9/8.
//

#include "../../include/handler/UserHandler.h"
#include "handler/UserHandler.h"
#include "nlohmann/json.hpp"
#include <boost/uuid/string_generator.hpp>
#include <optional>

HttpResponse UserHandler::FetchFriends(const HttpRequest&, const boost::uuids::uuid& my_uuid) {
    try {
        const auto friends = userService_.fetchFriends(my_uuid);
        nlohmann::json body;
        body["friends"] = nlohmann::json::array();
        for (const auto& user : friends) {
            nlohmann::json friend_details;
            friend_details["id"] = boost::uuids::to_string(user.id);
            friend_details["username"] = user.username;
            friend_details["display_name"] = user.display_name;
            body["friends"].push_back(std::move(friend_details));
        }
        HttpResponse response;
        response.status = HttpStatus::Ok;
        response.headers["Content-Type"] = "application/json";
        response.body = body.dump();
        return response;
    }
    catch (const DatabaseError& error) {
        std::cerr << "[FetchFriends] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "fetch_friends_failed", "Unable to fetch friends");
    }
}

HttpResponse UserHandler::ProcessFriendshipRequest(
    const HttpRequest& request, const boost::uuids::uuid& my_uuid) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    const auto idField = body.find("request_id");
    const auto processField = body.find("process");
    if (idField == body.end() || !idField->is_string() ||
        idField->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_request_id", "request_id must be a UUID string");
    }
    if (processField == body.end() || !processField->is_string() ||
        processField->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_process", "process must be accepted, rejected or cancelled");
    }

    boost::uuids::uuid requestId;
    try {
        requestId = boost::uuids::string_generator{}(idField->get<std::string>());
    } catch (const std::runtime_error&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_request_id", "request_id must be a UUID string");
    }
    const std::string process = processField->get<std::string>();
    try {
        const auto result = userService_.processFriendshipRequest(my_uuid, requestId, process);
        switch (result) {
            case FriendRequestProcessResult::Success:
            case FriendRequestProcessResult::AlreadyProcessed: {
                HttpResponse response;
                response.status = HttpStatus::Ok;
                response.headers["Content-Type"] = "application/json";
                response.body = nlohmann::json{
                    {"request_id", boost::uuids::to_string(requestId)},
                    {"status", process},
                    {"already_processed", result == FriendRequestProcessResult::AlreadyProcessed}
                }.dump();
                return response;
            }
            case FriendRequestProcessResult::NotFound:
                return ErrorResponseMaker(HttpStatus::Not_Found,
                    "friend_request_not_found", "Friend request not found");
            case FriendRequestProcessResult::Forbidden:
                return ErrorResponseMaker(HttpStatus::Forbidden,
                    "friend_request_forbidden", "You cannot perform this action on this request");
            case FriendRequestProcessResult::Conflict:
                return ErrorResponseMaker(HttpStatus::Conflict,
                    "friend_request_already_processed", "Friend request has a different final status");
            case FriendRequestProcessResult::InvalidAction:
                return ErrorResponseMaker(HttpStatus::Bad_Request,
                    "invalid_process", "process must be accepted, rejected or cancelled");
        }
        throw std::logic_error("Unhandled FriendRequestProcessResult");
    } catch (const DatabaseError& error) {
        // Log codes only: query details can contain user data.
        std::cerr << "[ProcessFriendshipRequest] database failure; SQLSTATE="
                  << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "process_friend_request_failed", "Unable to process friend request");
    }
}

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

UserHandler::UserHandler(UserService& userService,RegisterService& registerService,LoginService &loginService,KeyService& keyService): userService_(userService),registerService_(registerService)
,loginService_(loginService),keyService_(keyService){}

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
    const auto usernameIterator =body.find("username");
    if (usernameIterator == body.end() ||!usernameIterator->is_string() ||usernameIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_register_username",
            "user name must be a non-empty string"
        );
    }
    std::string userEmail =emailIterator->get<std::string>();
    std::string userPassword =passwordIterator->get<std::string>();
    std::string userDisplayName =displayNameIterator->get<std::string>();
    std::string userName =usernameIterator->get<std::string>();
    userEmail=toLower(conversionInput(userEmail));
    userPassword=conversionInput(userPassword);
    userDisplayName=conversionInput(userDisplayName);
    userName=conversionInput(userName);
    RegisterResult registerResult= registerService_.RegisterUserByEmail(userEmail,userPassword,userDisplayName,userName);
    auto user=registerResult.result;
    if (std::holds_alternative<User>(user)) {
        const User& user = std::get<User>(registerResult.result);
        nlohmann::json body;
        body["id"] = boost::uuids::to_string(user.id);
        body["display_name"] = user.display_name;
        body["username"]=user.username;
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

        case RegisterError::UsernameExists:
            return ErrorResponseMaker(
                HttpStatus::Conflict,
                "username_already_exists",
                "This username is already registered"
            );
        case RegisterError::InvalidUsername:
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_user_name",
                "Invalid user name"
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
    std::string device_id;
    const auto deviceIterator = body.find("device_id");
    if (deviceIterator != body.end() && !deviceIterator->is_null()) {
        if (!deviceIterator->is_string()) {
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_device_id",
                "device_id must be a UUID string or null"
            );
        }

        device_id = deviceIterator->get<std::string>();
    }
    std::optional<boost::uuids::uuid> parsedDeviceId;
    if (!device_id.empty()) {
        try {
            parsedDeviceId = boost::uuids::string_generator{}(device_id);
        } catch (const std::runtime_error&) {
            return ErrorResponseMaker(
                HttpStatus::Bad_Request,
                "invalid_device_id",
                "device_id must be a valid UUID"
            );
        }
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
        if (!parsedDeviceId) {
            // Missing, null or empty device_id: no UUID parsing or device lookup.
            //unregistered进入注册流程，unavailable进入恢复流程
            body["device_status"] = "unregistered";
        } else {
            auto verified = keyService_.verifyDeviceId(user.id, *parsedDeviceId);
            if (verified==std::nullopt) {
                body["device_status"] ="unregistered";
            }
            else
            body["device_status"] = verified ? "verified" : "unavailable";
        }
        body["id"] = boost::uuids::to_string(user.id);
        body["display_name"] = user.display_name;
        body["username"] = user.username;
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
HttpResponse UserHandler::FindUserByUsername(const HttpRequest& request,const boost::uuids::uuid& my_uuid) {
    HttpResponse response;
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
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
    const auto userNameIterator =body.find("username");
    if (userNameIterator == body.end() ||!userNameIterator->is_string() ||userNameIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_username",
            "user name must be a non-empty string"
        );
    }
    std::string username =userNameIterator->get<std::string>();
    auto result=userService_.findUserByUserName(username);
    if (result==std::nullopt)
        return ErrorResponseMaker(HttpStatus::Not_Found,"user_not_found","User not found");
    if (result->id==my_uuid) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,"cannot_find_myself","Can not Search for Myself");
    }
    nlohmann::json response_body;
    response_body["id"] = boost::uuids::to_string(result->id);
    response_body["display_name"] =result->display_name;
    response_body["username"] = result->username;
    response.body = response_body.dump();
    response.status=HttpStatus::Ok;
    return response;



}
HttpResponse UserHandler::RequestFriendship(const HttpRequest& request,const boost::uuids::uuid& my_uuid) {
    HttpResponse response;
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
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
    const auto uuidIterator =body.find("friend_id");
    const auto messageIterator =body.find("message");
    std::string message;
    if (uuidIterator == body.end() ||!uuidIterator->is_string() ||uuidIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_friend_id",
            "Must has friend_id"
        );
    }
    std::string friend_uuid_string =uuidIterator->get<std::string>();
    boost::uuids::uuid friend_uuid=boost::uuids::string_generator{}(friend_uuid_string);
    if (friend_uuid==my_uuid) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,"cannot_request_friendship_myself","Can not request Myself");
    }
    if (messageIterator == body.end() ||!messageIterator->is_string() ||messageIterator->get_ref<const std::string&>().empty())
        message="";
    else
        message=messageIterator->get<std::string>();

    try {
        bool result=userService_.requestFriendship(my_uuid,friend_uuid,message);
        if (result) {
            response.status=HttpStatus::Ok;
            return response;
        }
        else {
            return ErrorResponseMaker(HttpStatus::Conflict,"request_already_exists","Friendship request already exists");
        }

    }
    catch (const std::exception& error) {
        std::cerr
            << "[RequestFriendship] failed: "
            << error.what()
            << '\n';

        return ErrorResponseMaker(
            HttpStatus::Service_Unavailable,
            "request_friendship_failed",
            "Unable to send friend request"
        );
    }


}

HttpResponse UserHandler::FetchUnprocessedFriendship(const HttpRequest& request,const boost::uuids::uuid& my_uuid) {
    HttpResponse response;
    try {
        nlohmann::json body;
        auto result=userService_.fetchFriendshipRequest(my_uuid);
        if (result==std::nullopt) {
            response.status=HttpStatus::Ok;
            body["unprocessed_requests"] = "None";
            response.body = body.dump();
            return response;
        }
        response.status=HttpStatus::Ok;
        body["unprocessed_requests"] = nlohmann::json::array();
        for (std::size_t i = 0; i < result->size(); ++i) {
            const auto& record = (*result)[i];
            nlohmann::json request_details;
            request_details["request_id"] =boost::uuids::to_string(record.request_id);
            request_details["id"] =boost::uuids::to_string(record.id);
            request_details["username"] = record.username;
            request_details["message"] = record.message;
            request_details["display_name"] = record.display_name;
            body["unprocessed_requests"].push_back(std::move(request_details));
        }
        response.headers["Content-Type"] = "application/json";
        response.body = body.dump();
        return response;
    }
    catch (const std::exception& error) {
        std::cerr<<error.what()<<std::endl;
        return ErrorResponseMaker(HttpStatus::Service_Unavailable,"fetch_new_request_failed","Failed to fetch newly created request");
    }

}


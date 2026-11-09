#include "common/Logger.h"
//
// Created by yangb on 2026/9/8.
//

#include "../../include/handler/UserHandler.h"
#include "handler/UserHandler.h"
#include "handler/LoginTicketResponse.h"
#include "service/LoginTicketService.h"
#include "nlohmann/json.hpp"
#include <boost/uuid/string_generator.hpp>
#include <algorithm>
#include <optional>
#include <string_view>
#include <system_error>

HttpResponse UserHandler::FetchFriends(const HttpRequest&, const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserHandler", "FetchFriends"};
    try {
        const auto friends = userService_.fetchFriends(my_uuid);
        nlohmann::json body;
        body["friends"] = nlohmann::json::array();
        for (const auto& record : friends) {
            const auto& user = record.user;
            nlohmann::json friend_details;
            friend_details["id"] = boost::uuids::to_string(user.id);
            friend_details["username"] = user.username;
            friend_details["display_name"] = user.display_name;
            friend_details["avatar_id"] = nullptr;
            if (record.avatar_id) friend_details["avatar_id"] = boost::uuids::to_string(*record.avatar_id);
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
    backend::logging::Scope operationLog{"UserHandler", "ProcessFriendshipRequest"};
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

UserHandler::UserHandler(UserService& userService,RegisterService& registerService,
    LoginService &loginService,KeyService& keyService,WebSocketPool &web_socket_pool, LoginTicketService& tickets): userService_(userService),registerService_(registerService)
,loginService_(loginService),keyService_(keyService),webSocketPool_(web_socket_pool), tickets_(tickets){}

HttpResponse UserHandler::Register(const HttpRequest &request) {
    backend::logging::Scope operationLog{"UserHandler", "Register"};
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
    backend::logging::Scope operationLog{"UserHandler", "LoginByEmail"};
    const auto makeResult = [](HttpStatus status, const char* code) {
        HttpResponse response;
        response.status = status;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{{"code", code}}.dump();
        return response;
    };
    if (request.body.empty()) {
        return makeResult(HttpStatus::Bad_Request, "missing_request_body");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return makeResult(HttpStatus::Bad_Request, "invalid_json");
    }
    const auto email = body.find("email");
    if (email == body.end() || !email->is_string() || email->get_ref<const std::string&>().empty()) {
        return makeResult(HttpStatus::Bad_Request, "missing_register_email");
    }
    const auto password = body.find("password");
    if (password == body.end() || !password->is_string() || password->get_ref<const std::string&>().empty()) {
        return makeResult(HttpStatus::Bad_Request, "missing_register_password");
    }
    try {
        const auto userEmail = toLower(conversionInput(email->get<std::string>()));
        const auto userPassword = conversionInput(password->get<std::string>());
        const auto result = loginService_.loginByEmail(userEmail, userPassword);
        switch (result.login) {
            case LoginError::Success: {
                if (!result.login_ticket) return makeResult(HttpStatus::Internal_Server_Error, "internal_error");
                auto response = makeResult(HttpStatus::Ok, "login_success");
                response.body = nlohmann::json{{"code", "login_success"},
                    {"login_ticket", *result.login_ticket}, {"expires_in", 300}}.dump();
                return response;
            }
            case LoginError::NoUserFound:
            case LoginError::InvalidCredentials:
                return makeResult(HttpStatus::Unauthorized, "invalid_credentials");
            case LoginError::NotActiveUser:
                return makeResult(HttpStatus::Forbidden, "account_disabled");
            case LoginError::TooManyRequests:
                return makeResult(HttpStatus::Service_Unavailable, "too_many_requests");
            case LoginError::ServiceUnavailable:
                return makeResult(HttpStatus::Service_Unavailable, "service_unavailable");
            case LoginError::InternalError:
                return makeResult(HttpStatus::Internal_Server_Error, "internal_error");
        }
        return makeResult(HttpStatus::Internal_Server_Error, "internal_error");
    } catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08")) {
            return makeResult(HttpStatus::Service_Unavailable, "service_unavailable");
        }
        return makeResult(HttpStatus::Internal_Server_Error, "internal_error");
    } catch (const std::exception&) {
        return makeResult(HttpStatus::Internal_Server_Error, "internal_error");
    }
}
HttpResponse UserHandler::getUser(const HttpRequest& request)
{
    backend::logging::Scope operationLog{"UserHandler", "getUser"};
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
    backend::logging::Scope operationLog{"UserHandler", "verifyaccess"};
    std::cout<<"access_token received"<<std::endl;
    HttpResponse response;
    response.status=HttpStatus::Ok;
    nlohmann::json body;
    body["success"] = "success";
    return response;
}
HttpResponse UserHandler::FindUserByUsername(const HttpRequest& request,const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserHandler", "FindUserByUsername"};
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
    backend::logging::Scope operationLog{"UserHandler", "RequestFriendship"};
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
        auto result=userService_.requestFriendship(my_uuid,friend_uuid,message);
        if (result!=std::nullopt) {
            response.status=HttpStatus::Ok;
            try {
                nlohmann::json online_push;
                nlohmann::json online_send_request_body;
                online_send_request_body["request_id"]=boost::uuids::to_string(result->request_id);
                online_send_request_body["id"]=boost::uuids::to_string(result->id);
                online_send_request_body["display_name"]=result->display_name;
                online_send_request_body["username"]=result->username;
                online_send_request_body["message"] = result->message;
                online_push["type"] = "friend.request.received";
                online_push["unprocessed_requests"] =nlohmann::json::array({online_send_request_body});
                webSocketPool_.push_To_User(friend_uuid,online_push);
            } catch (const std::exception&) {
                // The request is already stored; recipients can fetch pending requests.
                std::cerr << "[RequestFriendship] notification failed after storage\n";
            }
            return response;
        }
            return ErrorResponseMaker(HttpStatus::Conflict,"request_already_exists","Friendship request already exists");



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
    backend::logging::Scope operationLog{"UserHandler", "FetchUnprocessedFriendship"};
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

HttpResponse UserHandler::RevokeDevice(const HttpRequest& request,const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserHandler", "RevokeDevice"};
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    const auto passwordIterator =body.find("password");
    if (passwordIterator == body.end() ||!passwordIterator->is_string() ||passwordIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_password",
            "Password must be a non-empty string"
        );
    }
    std::string userPassword =passwordIterator->get<std::string>();
    const auto device_id_Iterator =body.find("deviceID");
    if (device_id_Iterator == body.end() ||!device_id_Iterator->is_string() ||device_id_Iterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_deviceID",
            "deviceID must be a non-empty string"
        );
    }
    const auto device_id_string = device_id_Iterator->get<std::string>();
    boost::uuids::uuid device_id;
    try {
        if (device_id_string.size() != 36 || device_id_string[8] != '-' ||
            device_id_string[13] != '-' || device_id_string[18] != '-' ||
            device_id_string[23] != '-') {
            throw std::invalid_argument("Invalid UUID format");
        }
        device_id = boost::uuids::string_generator{}(device_id_string);
        if (device_id.is_nil()) throw std::invalid_argument("Nil device UUID");
    } catch (const std::exception&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_deviceID", "deviceID must be a non-nil UUID string");
    }
    const auto successResponse = [&](bool alreadyRevoked) {
        const bool websocketClosed = webSocketPool_.closeSession(my_uuid, device_id);
        HttpResponse response;
        response.status = websocketClosed ? HttpStatus::Ok : HttpStatus::Accepted;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"device_id", boost::uuids::to_string(device_id)},
            {"status", "revoked"}, {"already_revoked", alreadyRevoked},
            {"websocket_closed", websocketClosed}
        }.dump();
        return response;
    };
    try {
        // Use the password exactly as supplied; never log it or the request body.
        const auto result = userService_.revokeUserDevice(my_uuid, device_id, userPassword);
        if (const auto success = std::get_if<bool>(&result)) {
            if (*success) {
                return successResponse(false);
            }

            throw std::logic_error("Unexpected false revocation result");
        }
        switch (std::get<UserServiceErrorCode>(result)) {
            case UserServiceErrorCode::RevokedDevice:
                return successResponse(true);
            case UserServiceErrorCode::UserNotFound:
                return ErrorResponseMaker(HttpStatus::Not_Found,
                    "user_not_found", "Password login record not found");
            case UserServiceErrorCode::InvalidPassword:
                return ErrorResponseMaker(HttpStatus::Forbidden,
                    "invalid_password", "Password verification failed");
            case UserServiceErrorCode::NotYourDevice:
                return ErrorResponseMaker(HttpStatus::Forbidden,
                    "device_not_owned", "Device does not belong to the authenticated user");
            case UserServiceErrorCode::DeviceNotFound:
                return ErrorResponseMaker(HttpStatus::Not_Found,
                    "device_not_found", "Device not found");
            case UserServiceErrorCode::DeviceNotActive:
                return ErrorResponseMaker(HttpStatus::Conflict,
                    "device_not_active", "Only active devices can be revoked");
            default:
                throw std::logic_error("Unhandled revocation error");
        }
    } catch (const DatabaseError& error) {
        std::cerr << "[RevokeDevice] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08")) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Retry revocation with the same deviceID");
        }
        if (error.code() == "40001" || error.code() == "40P01") {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "retry_later", "Retry revocation with the same deviceID");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "revoke_device_failed", "Unable to revoke device");
    } catch (const std::exception&) {
        std::cerr << "[RevokeDevice] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "revoke_device_failed", "Unable to revoke device");
    }
}

HttpResponse UserHandler::DeviceVerification(const HttpRequest& request) {
    backend::logging::Scope operationLog{"UserHandler", "DeviceVerification"};
    if (request.body.empty()) return ErrorResponseMaker(HttpStatus::Bad_Request, "missing_request_body", "Request body is required");
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (!body.is_object()) return ErrorResponseMaker(HttpStatus::Bad_Request, "invalid_json", "Request body must be a JSON object");
    const auto ticket = body.find("login_ticket");
    if (ticket == body.end() || !ticket->is_string() || ticket->get_ref<const std::string&>().empty())
        return ErrorResponseMaker(HttpStatus::Unauthorized, "invalid_login_ticket", "Login ticket is required");
    const auto device = body.find("device_id");
    if (device == body.end() || !device->is_string())
        return ErrorResponseMaker(HttpStatus::Bad_Request, "invalid_device_id", "Device must be a non-nil UUID");
    boost::uuids::uuid deviceId{};
    try {
        const auto& text = device->get_ref<const std::string&>();
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
            throw std::invalid_argument("UUID format");
        deviceId = boost::uuids::string_generator{}(text);
        if (deviceId.is_nil()) throw std::invalid_argument("Nil UUID");
    } catch (const std::exception&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request, "invalid_device_id", "Device must be a non-nil UUID");
    }
    try {
        return loginTicketResponse(tickets_.verifyDevice(ticket->get_ref<const std::string&>(), deviceId));
    } catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08"))
            return ErrorResponseMaker(HttpStatus::Service_Unavailable, "database_unavailable", "Login outcome uncertain; log in again");
        if (error.code() == "40001" || error.code() == "40P01")
            return ErrorResponseMaker(HttpStatus::Service_Unavailable, "retry_later", "Retry with the same ticket");
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error, "verification_device_failed", "Unable to complete login");
    } catch (const std::exception&) {
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error, "verification_device_failed", "Unable to complete login");
    }
}

HttpResponse UserHandler::uploadAvatar(const HttpRequest& request, const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserHandler", "UploadAvatar"};

    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request, "missing_request_body", "JPEG request body is required");
    }
    constexpr std::size_t max_avatar_bytes = 256 * 1024;
    if (request.body.size() > max_avatar_bytes) {
        return ErrorResponseMaker(HttpStatus::Payload_Too_Large, "avatar_too_large", "Avatar must be at most 256 KiB");
    }

    const auto equals_ignore_case = [](std::string_view left, std::string_view right) {
        const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch; };
        return std::equal(left.begin(), left.end(), right.begin(), right.end(), [&](char a, char b) { return lower(a) == lower(b); });
    };

    std::optional<std::string_view> content_type;
    for (const auto& [name, value] : request.headers) {
        if (equals_ignore_case(name, "transfer-encoding")) {
            return ErrorResponseMaker(HttpStatus::Bad_Request, "unsupported_avatar_transfer_encoding", "Send the JPEG with Content-Length");
        }

        // 普通 HTTP 解析器保留了头字段原有大小写，值可能包含前后空白。
        const auto start = value.find_first_not_of(" \t");
        const auto end = value.find_last_not_of(" \t");
        const auto trimmed = start == std::string::npos ? std::string_view{} : std::string_view{value}.substr(start, end - start + 1);
        if (equals_ignore_case(name, "content-encoding") && !equals_ignore_case(trimmed, "identity")) {
            return ErrorResponseMaker(HttpStatus::Unsupported_Media_Type, "unsupported_avatar_content_encoding", "Send the JPEG without HTTP content encoding");
        }
        if (equals_ignore_case(name, "content-type")) {
            if (content_type.has_value()) {
                return ErrorResponseMaker(HttpStatus::Bad_Request, "duplicate_content_type", "Only one Content-Type header is allowed");
            }
            content_type = trimmed;
        }
    }
    if (!content_type || !equals_ignore_case(*content_type, "image/jpeg")) {
        return ErrorResponseMaker(HttpStatus::Unsupported_Media_Type, "unsupported_avatar_content_type", "Content-Type must be image/jpeg");
    }
    const std::string_view jpeg_bytes{request.body};
    const auto result = userService_.uploadAvatar(my_uuid, jpeg_bytes);
    if (const auto* error = std::get_if<AvatarErrorCode>(&result)) {
        switch (*error) {
            case AvatarErrorCode::InvalidSize:
                return ErrorResponseMaker(HttpStatus::Unprocessable_Content, "invalid_avatar_dimensions", "Avatar must be exactly 512 x 512 pixels");
            case AvatarErrorCode::ParseFailed:
                return ErrorResponseMaker(HttpStatus::Bad_Request, "invalid_avatar_jpeg", "Avatar must be a complete and valid JPEG");
            case AvatarErrorCode::DecoderUnavailable:
                return ErrorResponseMaker(HttpStatus::Service_Unavailable, "avatar_decoder_unavailable", "Avatar validation is temporarily unavailable");
            case AvatarErrorCode::StorageFailed:
                return ErrorResponseMaker(HttpStatus::Internal_Server_Error, "avatar_storage_failed", "Unable to store avatar file");
            case AvatarErrorCode::UserUnavailable:
                return ErrorResponseMaker(HttpStatus::Forbidden, "avatar_user_unavailable", "An active account is required to upload an avatar");
            case AvatarErrorCode::DataBaseError:
                return ErrorResponseMaker(HttpStatus::Internal_Server_Error, "avatar_save_failed", "Unable to save avatar");
            case AvatarErrorCode::DatabaseUnavailable:
                return ErrorResponseMaker(HttpStatus::Service_Unavailable, "database_unavailable", "Avatar save could not be confirmed");
        }
        throw std::logic_error("Unhandled AvatarErrorCode");
    }

    HttpResponse response;
    response.status = HttpStatus::Ok;
    response.headers["Content-Type"] = "application/json";
    const nlohmann::json body = {{"avatar", {{"id", boost::uuids::to_string(std::get<boost::uuids::uuid>(result))}, {"size_bytes", jpeg_bytes.size()}}}};
    response.body = body.dump();
    return response;
}

HttpResponse UserHandler::get_avatar(const HttpRequest& request, const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserHandler", "GetAvatar"};
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    const auto avatarIdIterator = body.find("avatar_id");
    if (avatarIdIterator == body.end() || !avatarIdIterator->is_string()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_avatar_id", "avatar_id must be a UUID string");
    }

    boost::uuids::uuid avatar_id;
    try {
        avatar_id = boost::uuids::string_generator{}(avatarIdIterator->get_ref<const std::string&>());
    } catch (const std::runtime_error&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_avatar_id", "avatar_id must be a UUID string");
    }

    try {
        auto avatar = userService_.getAvatarByID(avatar_id, my_uuid);
        if (!avatar) {
            return ErrorResponseMaker(HttpStatus::Not_Found, "avatar_not_found", "Avatar not found");
        }
        HttpResponse response;
        response.status = HttpStatus::Ok;
        response.headers["Content-Type"] = "image/jpeg";
        response.body = std::move(*avatar);
        return response;
    } catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "avatar_fetch_failed", "Unable to fetch avatar");
    } catch (const std::system_error&) {
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "avatar_read_failed", "Unable to read avatar file");
    }
}

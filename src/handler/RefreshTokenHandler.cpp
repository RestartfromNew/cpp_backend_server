//
// Created by yangb on 2026/9/17.
//

#include "handler/RefreshTokenHandler.h"
RefreshTokenHandler::RefreshTokenHandler(RefreshTokenService &refreshTokenService,AccessTokenService &accessTokenService)
    :refreshTokenService_(refreshTokenService),accessTokenService_(accessTokenService){}
HttpResponse RefreshTokenHandler::VerifyRefreshToken(const HttpRequest& request) {
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
    const auto refreshTokenIterator=body.find("refreshToken");
    if (refreshTokenIterator == body.end() ||!refreshTokenIterator->is_string() ||refreshTokenIterator->get_ref<const std::string&>().empty()) {
        return ErrorResponseMaker(
            HttpStatus::Bad_Request,
            "missing_refreshToken",
            "Missing refresh token"
        );
    }
    std::string refreshToken = refreshTokenIterator->get_ref<const std::string&>();
    auto refreshTokenResult=refreshTokenService_.validate(refreshToken);
  if (std::holds_alternative<RefreshTokenStatus>(refreshTokenResult)) {
      const auto& status = std::get<RefreshTokenStatus>(refreshTokenResult);
      if (status==RefreshTokenStatus::Invalid)
          return ErrorResponseMaker(HttpStatus::Unauthorized,"invalid_refreshToken","Invalid refresh token");
      if (status==RefreshTokenStatus::Expired)
          //过期要求重新登录
          return ErrorResponseMaker(HttpStatus::Unauthorized,"invalid_refreshToken","Refresh token is expired");
  }
    //没过期
    const auto& userId = std::get<std::string>(refreshTokenResult);
    std::string accessToken=accessTokenService_.generateAccessToken(userId);
    nlohmann::json json;
    json["refresh_token"] = refreshToken;
    json["access_token"] = accessToken;
    json["id"]=userId;
    response.body=json.dump();
    response.status=HttpStatus::Ok;
    return response;
}
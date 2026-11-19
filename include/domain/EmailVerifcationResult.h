//
// Created by yangb on 2026/11/18.
//

#ifndef CPP_BACKEND_SERVER_EMAILVERIFCATIONRESULT_H
#define CPP_BACKEND_SERVER_EMAILVERIFCATIONRESULT_H
#include <boost/uuid/uuid.hpp>

enum class GetVerificationEmailCodeResponse {
    EmailRegistered, ToMuchRequest, Send,UnknowError
};
enum class VerifyEmailCodeResponse {
  CodeDoNotMatch, CodeExpired,ToMuchTry,Verified
};
// Verified 时携带匹配记录的 ID，注册事务用它重新校验并消费。
struct EmailCodeVerificationResult {
    VerifyEmailCodeResponse status = VerifyEmailCodeResponse::CodeDoNotMatch;
    boost::uuids::uuid record_id{};
};
#endif //CPP_BACKEND_SERVER_EMAILVERIFCATIONRESULT_H

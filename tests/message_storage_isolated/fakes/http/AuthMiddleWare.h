#pragma once
#include <variant>
#include "boost/uuid/uuid.hpp"
enum class AuthError {UnAuthorized,Expired};
struct AuthResult {std::variant<boost::uuids::uuid,AuthError> result;};
class AuthMiddleWare {public:AuthResult authenticate(const std::string& token)const {
 if(token=="Bearer valid")return {boost::uuids::uuid{"11111111-1111-4111-8111-111111111111"}};
 return {token=="Bearer expired"?AuthError::Expired:AuthError::UnAuthorized};
}};

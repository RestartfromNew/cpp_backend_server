//
// Created by yangb on 2026/9/16.
//

#include "service/LoginService.h"
LoginService::LoginService(UserRepository &userRepository, RefreshTokenService &refreshTokenService,AccessTokenService &accessTokenService)
:userRepository_(userRepository),accessTokenService_(accessTokenService),refreshTokenService_(refreshTokenService){};
bool LoginService::verifyPassword( const std::string &password_hash, const std::string &password ) {
    //password的hash会生成随机盐
    return crypto_pwhash_str_verify(password_hash.c_str(), // 数据库存储的哈希字符串
    password.data(),       // 用户本次输入的原始密码
    password.size()        // 密码字节数
    ) == 0;
}

LoginServiceResult LoginService::loginByEmail(const std::string &email, const std::string &password) {
    std::cout<<"loginService activated"<< std::endl;
    //默认传入的时候格式已经正确
    auto password_login_record=userRepository_.FindLoginRecordByEmail(email);
    if (!password_login_record)
        //该用户不存在
        return LoginServiceResult{LoginError::NoUserFound};
    //验证密码
    if (!verifyPassword(password_login_record->password_hash, password))
        return LoginServiceResult{LoginError::InvalidCredentials};
    //验证是否是激活状态
    if (!password_login_record->is_active)
        return LoginServiceResult{LoginError::NotActiveUser};
    //读取jwt secret
    const char* secret=std::getenv("JWT_SECRET");
    if (secret == nullptr || secret[0] == '\0') {
        throw std::runtime_error("JWT_SECRET is not set or is empty");
    }
    std:: string userId=boost::uuids::to_string(password_login_record->id);
    std::string accessToken=accessTokenService_.generateAccessToken(userId);
    std::string refreshToken=refreshTokenService_.generateRefreshToken();
    std::string refreshTokenHashed=refreshTokenService_.RefreshTokenHash(refreshToken);
    userRepository_.InsertRefreshToken(password_login_record->id,refreshTokenHashed);
    User user{password_login_record->id,password_login_record->display_name};
    return LoginServiceResult{UserToken{user,accessToken,refreshToken}};
}







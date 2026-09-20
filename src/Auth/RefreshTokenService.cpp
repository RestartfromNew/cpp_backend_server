//
// Created by yangb on 2026/9/16.
//

#include "Auth/RefreshTokenService.h"
RefreshTokenService::RefreshTokenService(RefreshTokenRepository &refreshTokenRepository):refreshTokenRepository_(refreshTokenRepository){};
std::string RefreshTokenService::generateRefreshToken() {
    // 32 字节，也就是 256 位随机数据。
    unsigned char randomBytes[32];
    randombytes_buf(randomBytes, sizeof randomBytes);
    // 转为方便 HTTP 传输的 Base64URL 字符串。
    char encoded[sodium_base64_ENCODED_LEN(sizeof randomBytes,sodium_base64_VARIANT_URLSAFE_NO_PADDING)];
    sodium_bin2base64(
        encoded,
        sizeof encoded,
        randomBytes,
        sizeof randomBytes,
        sodium_base64_VARIANT_URLSAFE_NO_PADDING
    );
    return std::string{encoded};
}
std::variant<RefreshTokenStatus,std::string> RefreshTokenService::validate(const std::string& refreshToken_origin) {
    std::string refreshToken_old_hash=RefreshTokenHash(refreshToken_origin);
    auto record=refreshTokenRepository_.getRefreshTokenRecord(refreshToken_old_hash);
    if (record==std::nullopt)
        return Invalid;
    if (record->revoked_at.has_value())
        return Expired;
    if (record->expires_at<=std::chrono::system_clock::now()) {
        //如果没有被标记失效
        refreshTokenRepository_.revokeRefreshToken(refreshToken_old_hash);
        return Expired;
    }
    std::string user_id = boost::uuids::to_string(record->user_id);
    return user_id;

}
//验证旧的token是否过期和合法，如果过期就生成新的token
std::variant<std::string,bool> RefreshTokenService::fleshRefreshToken(const std::string& refreshToken_old) {
  return false;
}
//定时检查并删除失效token
std::size_t RefreshTokenService::deleteExpiredBefore(std::chrono::system_clock::time_point cutoff) {
    return refreshTokenRepository_.deleteExpiredBefore(cutoff);
}
std::string RefreshTokenService::RefreshTokenHash(const std::string& refreshToken_origin) {
    // 保存二进制哈希，默认长度为 32 字节。
    //使用BLAKE2b算法，相同的输入产生的hash相同
    unsigned char hash[crypto_generichash_BYTES];
    if (crypto_generichash(
            hash,
            sizeof hash,
            reinterpret_cast<const unsigned char*>(
                refreshToken_origin.data()
            ),
            refreshToken_origin.size(),
            nullptr, // 不使用密钥。
            0
        ) != 0) {
        throw std::runtime_error(
            "Refresh token hashing failed"
        );
        }
    // 二进制不能直接当 C 字符串使用。
    // 转成十六进制：每字节两个字符，额外一个位置保存 '\0'。
    char hex[sizeof hash * 2 + 1];
    sodium_bin2hex(hex,sizeof hex,hash,sizeof hash);
    return std::string{hex};
}
#include "JwtUtil.h"
#include <jwt-cpp/jwt.h>
#include <chrono>

std::string JwtUtil::generateAccessToken(int64_t userId) {
    auto now = std::chrono::system_clock::now();
    auto expiresAt = now + std::chrono::hours(2);

    return jwt::create()
        .set_issuer("todo_app_auth")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(std::to_string(userId)))
        .set_payload_claim("token_type", jwt::claim(std::string("access")))
        .set_issued_at(now)
        .set_expires_at(expiresAt)
        .sign(jwt::algorithm::hs256{SECRET_KEY});
}

std::string JwtUtil::generateRefreshToken(int64_t userId) {
    auto now = std::chrono::system_clock::now();
    auto expiresAt = now + std::chrono::hours(24 * 30); // 30天

    return jwt::create()
        .set_issuer("todo_app_auth")
        .set_type("JWS")
        .set_payload_claim("user_id", jwt::claim(std::to_string(userId)))
        .set_payload_claim("token_type", jwt::claim(std::string("refresh")))
        .set_issued_at(now)
        .set_expires_at(expiresAt)
        .sign(jwt::algorithm::hs256{SECRET_KEY});
}

int64_t JwtUtil::verifyAccessToken(const std::string& token) {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{SECRET_KEY})
            .with_issuer("todo_app_auth")
            .with_claim("token_type", jwt::claim(std::string("access")));

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);  // 过期或签名错误会抛异常

        std::string userIdStr = decoded.get_payload_claim("user_id").as_string();
        return std::stoll(userIdStr);

    } catch (const std::exception& e) {
        // token 无效、过期、签名错误等都走这里
        return -1;
    }
}

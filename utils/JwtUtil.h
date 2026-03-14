#pragma once
#include <string>
#include <cstdint>

class JwtUtil {
public:
    // 密钥 (生产环境中应放在配置文件或环境变量中)
    static constexpr const char* SECRET_KEY = "TodoApp_Super_Secret_Key_2026";

    // 生成 Access Token (有效期 2 小时)
    static std::string generateAccessToken(int64_t userId);

    // 生成 Refresh Token (有效期 30 天)
    static std::string generateRefreshToken(int64_t userId);

    // 验证 Access Token，成功返回 userId，失败返回 -1
    static int64_t verifyAccessToken(const std::string& token);
};

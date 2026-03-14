#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>
#include "../utils/JwtUtil.h"

using namespace drogon;

// 1. 将错误响应封装为静态全局函数，彻底脱离类实例的生命周期，杜绝 this 指针失效
static HttpResponsePtr createErrorResp(int code, const std::string& msg) {
    Json::Value ret;
    ret["code"] = code;
    ret["msg"] = msg;
    return HttpResponse::newHttpJsonResponse(ret);
}

class AuthController : public HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::refreshToken, "/api/auth/refresh", Post);
    ADD_METHOD_TO(AuthController::registerUser, "/api/auth/register", Post);
    ADD_METHOD_TO(AuthController::loginUser, "/api/auth/login", Post);
    METHOD_LIST_END

    Task<HttpResponsePtr> registerUser(HttpRequestPtr req) {
        LOG_DEBUG << "[Auth] ---- registerUser API Triggered ----";
        try {
            auto jsonReq = req->getJsonObject();
            if (!jsonReq) {
                LOG_ERROR << "[Auth] JSON parsing failed or body is empty";
                co_return createErrorResp(400, "Invalid JSON format");
            }
            if (!(*jsonReq)["username"].isString() || !(*jsonReq)["password"].isString()) {
                LOG_ERROR << "[Auth] Missing username or password fields";
                co_return createErrorResp(400, "Missing username or password");
            }

            std::string username = (*jsonReq)["username"].asString();
            std::string password = (*jsonReq)["password"].asString();
            LOG_DEBUG << "[Auth] 1. Parsed Username: " << username;

            auto db = app().getDbClient();
            if (!db) {
                LOG_ERROR << "[Auth] FATAL: DB Client is null!";
                co_return createErrorResp(500, "Database connection error");
            }

            LOG_DEBUG << "[Auth] 2. Executing SELECT check for existing user...";
            auto checkResult = co_await db->execSqlCoro("SELECT id FROM users WHERE username = ?", username);
            LOG_DEBUG << "[Auth] 3. SELECT check completed.";

            if (!checkResult.empty()) {
                LOG_DEBUG << "[Auth] Username already exists.";
                co_return createErrorResp(409, "Username already exists");
            }

            LOG_DEBUG << "[Auth] 4. Hashing password...";
            std::string salt = "todo_salt_";
            std::string passwordHash = utils::getSha256(salt + password);

            LOG_DEBUG << "[Auth] 5. Executing INSERT...";
            co_await db->execSqlCoro("INSERT INTO users (username, password_hash) VALUES (?, ?)", username, passwordHash);
            LOG_DEBUG << "[Auth] 6. INSERT completed.";

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Registration successful";
            
            LOG_DEBUG << "[Auth] 7. Returning success response.";
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const orm::DrogonDbException &e) {
            // 改为了最安全的 e.what()
            LOG_ERROR << "[Auth] DB Exception: " << e.base().what();
            co_return createErrorResp(500, std::string("DB Error: ") + e.base().what());
        } catch (const std::exception &e) {
            LOG_ERROR << "[Auth] STD Exception: " << e.what();
            co_return createErrorResp(500, std::string("Internal Error: ") + e.what());
        } catch (...) {
            LOG_ERROR << "[Auth] Unknown Exception occurred!";
            co_return createErrorResp(500, "Unknown Error");
        }
    }

    Task<HttpResponsePtr> loginUser(HttpRequestPtr req) {
        LOG_DEBUG << "[Auth] ---- loginUser API Triggered ----";
        try {
            auto jsonReq = req->getJsonObject();
            if (!jsonReq || !(*jsonReq)["username"].isString() || !(*jsonReq)["password"].isString()) {
                co_return createErrorResp(400, "Invalid JSON format");
            }

            std::string username = (*jsonReq)["username"].asString();
            std::string password = (*jsonReq)["password"].asString();
            std::string salt = "todo_salt_";
            std::string inputHash = utils::getSha256(salt + password);

            LOG_DEBUG << "[Auth] 1. Querying user...";
            auto db = app().getDbClient();
            auto result = co_await db->execSqlCoro("SELECT id, password_hash FROM users WHERE username = ?", username);
            LOG_DEBUG << "[Auth] 2. Query done. Result count: " << result.size();
            
            if (result.empty() || result[0]["password_hash"].as<std::string>() != inputHash) {
                co_return createErrorResp(401, "Invalid username or password");
            }

            int64_t userId = result[0]["id"].as<int64_t>();
            LOG_DEBUG << "[Auth] 3. Generating tokens for UID: " << userId;

            std::string accessToken = JwtUtil::generateAccessToken(userId);
            std::string refreshToken = JwtUtil::generateRefreshToken(userId);

            LOG_DEBUG << "[Auth] 4. Updating refresh token in DB...";
            co_await db->execSqlCoro(
                "UPDATE users SET refresh_token = ?, refresh_token_expires_at = DATE_ADD(NOW(), INTERVAL 30 DAY) WHERE id = ?", 
                refreshToken, userId
            );
            LOG_DEBUG << "[Auth] 5. DB updated.";

            Json::Value data;
            data["access_token"] = accessToken;
            data["refresh_token"] = refreshToken;

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Login successful";
            ret["data"] = data;

            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[Auth] Exception: " << e.what();
            co_return createErrorResp(500, std::string("Error: ") + e.what());
        } catch (...) {
            LOG_ERROR << "[Auth] Unknown Exception!";
            co_return createErrorResp(500, "Unknown Error");
        }
    }

    // 3. 使用长令牌换取新的短令牌 (无感刷新机制)
    Task<HttpResponsePtr> refreshToken(HttpRequestPtr req) {
        try {
            auto jsonReq = req->getJsonObject();
            if (!jsonReq || !(*jsonReq)["refresh_token"].isString()) {
                co_return createErrorResp(400, "Missing refresh_token");
            }
            std::string rToken = (*jsonReq)["refresh_token"].asString();

            auto db = app().getDbClient();
            // 极其严谨的校验：凭证必须存在，且当前时间必须小于过期时间
            auto result = co_await db->execSqlCoro(
                "SELECT id FROM users WHERE refresh_token = ? AND refresh_token_expires_at > NOW()",
                rToken
            );

            if (result.empty()) {
                // 如果找不到或者已过期，直接返回 401，让前端强行退回登录页
                co_return createErrorResp(401, "Refresh token invalid or expired. Please login again.");
            }

            int64_t userId = result[0]["id"].as<int64_t>();

            // 重新签发一张全新的短令牌
            std::string newAccessToken = JwtUtil::generateAccessToken(userId);

            Json::Value data;
            data["access_token"] = newAccessToken;

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Token refreshed successfully";
            ret["data"] = data;

            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[Auth] Refresh Token Error: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }
};

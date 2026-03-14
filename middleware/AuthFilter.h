#include <drogon/HttpMiddleware.h>
#include <jwt-cpp/jwt.h>
#include "../utils/JwtUtil.h"

using namespace drogon;

class AuthFilter : public HttpMiddleware<AuthFilter> {
public:
    void invoke(const HttpRequestPtr &req,
                MiddlewareNextCallback &&nextCb,
                MiddlewareCallback &&mcb) override {

        std::string authHeader = req->getHeader("Authorization");
        if (authHeader.empty() || authHeader.find("Bearer ") != 0) {
            Json::Value ret;
            ret["code"] = 401;
            ret["msg"] = "Missing or invalid Authorization header";
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k401Unauthorized);
            mcb(resp);
            return;
        }

        std::string token = authHeader.substr(7);
        int64_t userId = JwtUtil::verifyAccessToken(token);
        if (userId < 0) {
            Json::Value ret;
            ret["code"] = 401;
            ret["msg"] = "Token invalid or expired";
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k401Unauthorized);
            mcb(resp);
            return;
        }

        req->attributes()->insert("user_id", userId);
        nextCb([mcb = std::move(mcb)](const HttpResponsePtr &resp) {
            mcb(resp);
        });
    }
};

// 强制实例化，触发 DrObject 静态注册
template class drogon::DrObject<AuthFilter>;

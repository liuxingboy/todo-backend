#include <drogon/drogon.h>
#include "middleware/AuthFilter.h"  // ← 改成 .h
using namespace drogon;

int main() {
    // 👇 加上这一行：设置全局日志级别为 INFO (屏蔽 DEBUG 和 TRACE)
    app().setLogLevel(trantor::Logger::kInfo);

    app().setClientMaxBodySize(1024 * 1024 * 1024); //设置上传文件上限大小1GB

    LOG_INFO << "Initializing Database Connection Pool...";

    orm::MysqlConfig mysqlConfig;
    mysqlConfig.host             = "127.0.0.1";
    mysqlConfig.port             = 3306;
    mysqlConfig.databaseName     = "todo_db";
    mysqlConfig.username         = "todo_user";
    mysqlConfig.password         = "402360";
    mysqlConfig.connectionNumber = 5;
    mysqlConfig.name             = "default";
    mysqlConfig.isFast           = false;
    mysqlConfig.characterSet     = "utf8mb4";
    mysqlConfig.timeout          = -1.0;

    app().addDbClient(orm::DbConfig{mysqlConfig});

    // 👇 就是这4行：全局跨域放行配置 (CORS) 👇
    app().registerPostHandlingAdvice([](const HttpRequestPtr &, const HttpResponsePtr &resp) {
        resp->addHeader("Access-Control-Allow-Origin", "*"); // 允许所有前端跨域访问
        resp->addHeader("Access-Control-Allow-Methods", "OPTIONS, GET, POST, PUT, DELETE");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization"); // 允许携带 Token
    });

    LOG_INFO << "Starting Todo Backend Server on 0.0.0.0:8080";
    app().addListener("0.0.0.0", 8080).run();
    return 0;
}

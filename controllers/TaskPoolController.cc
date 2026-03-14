#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

using namespace drogon;

// 辅助函数：统一错误响应
static HttpResponsePtr createErrorResp(int code, const std::string& msg) {
    Json::Value ret;
    ret["code"] = code;
    ret["msg"] = msg;
    return HttpResponse::newHttpJsonResponse(ret);
}

class TaskPoolController : public HttpController<TaskPoolController> {
public:
    METHOD_LIST_BEGIN
    // 注意：这里的 "AuthFilter" 就是我们在中间件里定义的名字
    ADD_METHOD_TO(TaskPoolController::getPools, "/api/pools", Get, "AuthFilter");
    ADD_METHOD_TO(TaskPoolController::createPool, "/api/pools", Post, "AuthFilter");
    ADD_METHOD_TO(TaskPoolController::addTaskToPool, "/api/pools/task", Post, "AuthFilter");
    ADD_METHOD_TO(TaskPoolController::deletePool, "/api/pools", Delete, "AuthFilter");
    METHOD_LIST_END

    // 1. 获取用户所有的任务池及池内模板
    Task<HttpResponsePtr> getPools(HttpRequestPtr req) {
        // 从 AuthFilter 解析出的上下文中直接拿 user_id，极其优雅
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto db = app().getDbClient();

        try {
            // 获取用户的任务池分类
            auto poolsResult = co_await db->execSqlCoro("SELECT id, pool_name FROM task_pools WHERE user_id = ?", userId);
            
            Json::Value data(Json::arrayValue);
            for (auto row : poolsResult) {
                Json::Value pool;
                int64_t poolId = row["id"].as<int64_t>();
                pool["id"] = poolId;
                pool["pool_name"] = row["pool_name"].as<std::string>();

                // 获取该池子下的所有模板任务
                auto tasksResult = co_await db->execSqlCoro("SELECT id, content FROM pool_tasks WHERE pool_id = ?", poolId);
                Json::Value tasks(Json::arrayValue);
                for (auto tRow : tasksResult) {
                    Json::Value t;
                    t["id"] = tRow["id"].as<int64_t>();
                    t["content"] = tRow["content"].as<std::string>();
                    tasks.append(t);
                }
                pool["tasks"] = tasks;
                data.append(pool);
            }

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["data"] = data;
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[TaskPool] Error getting pools: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 2. 创建一个新的任务池分类 (例如："英语学习")
    Task<HttpResponsePtr> createPool(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["pool_name"].isString()) {
            co_return createErrorResp(400, "Missing pool_name");
        }
        std::string poolName = (*jsonReq)["pool_name"].asString();

        auto db = app().getDbClient();
        try {
            co_await db->execSqlCoro("INSERT INTO task_pools (user_id, pool_name) VALUES (?, ?)", userId, poolName);
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Pool created successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[TaskPool] Error creating pool: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 3. 向指定的池子里添加一个任务模板
    Task<HttpResponsePtr> addTaskToPool(HttpRequestPtr req) {
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["pool_id"].isInt64() || !(*jsonReq)["content"].isString()) {
            co_return createErrorResp(400, "Missing pool_id or content");
        }
        int64_t poolId = (*jsonReq)["pool_id"].asInt64();
        std::string content = (*jsonReq)["content"].asString();

        auto db = app().getDbClient();
        try {
            // 这里严谨一点：校验这个 pool_id 是不是属于当前用户的（防止越权）
            auto userId = req->attributes()->get<int64_t>("user_id");
            auto checkResult = co_await db->execSqlCoro("SELECT id FROM task_pools WHERE id = ? AND user_id = ?", poolId, userId);
            if (checkResult.empty()) {
                co_return createErrorResp(403, "Permission denied or pool does not exist");
            }

            co_await db->execSqlCoro("INSERT INTO pool_tasks (pool_id, content) VALUES (?, ?)", poolId, content);
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Task added to pool successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[TaskPool] Error adding task: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 4. 删除指定的任务池 (及其包含的所有任务模板)
    Task<HttpResponsePtr> deletePool(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["pool_id"].isInt64()) {
            co_return createErrorResp(400, "Missing pool_id");
        }
        int64_t poolId = (*jsonReq)["pool_id"].asInt64();

        auto db = app().getDbClient();
        try {
            // 第一步：极其严格的防越权校验，确认这个池子存在且属于当前用户
            auto checkResult = co_await db->execSqlCoro(
                "SELECT id FROM task_pools WHERE id = ? AND user_id = ?",
                poolId, userId
            );

            if (checkResult.empty()) {
                co_return createErrorResp(404, "Pool not found or permission denied");
            }

            // 第二步：级联清理 (非常重要！)
            // 先把这个池子里的所有任务模板删掉，防止产生无主孤儿数据
            co_await db->execSqlCoro("DELETE FROM pool_tasks WHERE pool_id = ?", poolId);

            // 第三步：斩草除根，删除池子本身
            co_await db->execSqlCoro("DELETE FROM task_pools WHERE id = ?", poolId);

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Pool and its tasks deleted successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[TaskPool] Error deleting pool: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }
};

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

using namespace drogon;

static HttpResponsePtr createErrorResp(int code, const std::string& msg) {
    Json::Value ret;
    ret["code"] = code;
    ret["msg"] = msg;
    return HttpResponse::newHttpJsonResponse(ret);
}

class MyDayController : public HttpController<MyDayController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(MyDayController::getTodayTasks, "/api/myday", Get, "AuthFilter");
    ADD_METHOD_TO(MyDayController::addTaskToMyDay, "/api/myday", Post, "AuthFilter");
    ADD_METHOD_TO(MyDayController::toggleTaskStatus, "/api/myday/status", Put, "AuthFilter");
    ADD_METHOD_TO(MyDayController::importTaskFromPool, "/api/myday/import", Post, "AuthFilter");
    METHOD_LIST_END

    // 1. 获取今天 (target_date = CURDATE()) 的任务
    Task<HttpResponsePtr> getTodayTasks(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto db = app().getDbClient();
        
        try {
            // 改用 target_date 和 status 字段
            auto result = co_await db->execSqlCoro(
                "SELECT id, content, status FROM my_day_tasks WHERE user_id = ? AND target_date = CURDATE()", 
                userId
            );
            
            Json::Value data(Json::arrayValue);
            for (auto row : result) {
                Json::Value task;
                task["id"] = row["id"].as<int64_t>();
                task["content"] = row["content"].as<std::string>();
                // 返回给前端时依然可以用布尔值，方便前端判断
                task["is_completed"] = row["status"].as<int>() == 1; 
                data.append(task);
            }
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["data"] = data;
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[MyDay] Error getting tasks: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 2. 添加任务到今天
    Task<HttpResponsePtr> addTaskToMyDay(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["content"].isString()) {
            co_return createErrorResp(400, "Missing task content");
        }
        std::string content = (*jsonReq)["content"].asString();

        auto db = app().getDbClient();
        try {
            // 插入时，设置 status 为 0，并把 target_date 设为今天 CURDATE()
            co_await db->execSqlCoro(
                "INSERT INTO my_day_tasks (user_id, content, status, target_date) VALUES (?, ?, 0, CURDATE())", 
                userId, content
            );
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Task added to My Day";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[MyDay] Error adding task: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 3. 更新任务状态
    Task<HttpResponsePtr> toggleTaskStatus(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["task_id"].isInt64() || !(*jsonReq)["is_completed"].isBool()) {
            co_return createErrorResp(400, "Missing task_id or is_completed flag");
        }
        int64_t taskId = (*jsonReq)["task_id"].asInt64();
        bool isCompleted = (*jsonReq)["is_completed"].asBool();

        auto db = app().getDbClient();
        try {
            // 同步更新 status 字段，如果完成了顺便把 completed_at 填上当前时间，如果取消完成就置空
            int statusValue = isCompleted ? 1 : 0;
            auto result = co_await db->execSqlCoro(
                "UPDATE my_day_tasks SET status = ?, completed_at = IF(? = 1, NOW(), NULL) WHERE id = ? AND user_id = ?", 
                statusValue, statusValue, taskId, userId
            );
            
            if (result.affectedRows() == 0) {
                co_return createErrorResp(404, "Task not found or permission denied");
            }

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Task status updated";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[MyDay] Error updating status: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 4. 从任务池导入任务到"我的一天" (GTD 核心流转)
    Task<HttpResponsePtr> importTaskFromPool(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["pool_task_id"].isInt64()) {
            co_return createErrorResp(400, "Missing pool_task_id");
        }
        int64_t poolTaskId = (*jsonReq)["pool_task_id"].asInt64();

        auto db = app().getDbClient();
        try {
            // 极其重要的防越权连表查询：
            // 必须确认这个 pool_task_id 对应的模板存在，且它所属的 task_pool 是当前用户的！
            auto checkResult = co_await db->execSqlCoro(
                "SELECT pt.content FROM pool_tasks pt "
                "JOIN task_pools tp ON pt.pool_id = tp.id "
                "WHERE pt.id = ? AND tp.user_id = ?",
                poolTaskId, userId
            );

            if (checkResult.empty()) {
                co_return createErrorResp(403, "Pool task not found or permission denied");
            }

            // 提取出模板的内容
            std::string content = checkResult[0]["content"].as<std::string>();

            // 完美复刻：实例化到今天的 my_day_tasks 表中
            // 记录下 pool_task_id 方便以后做数据统计（比如这个模板被完成了多少次）
            co_await db->execSqlCoro(
                "INSERT INTO my_day_tasks (user_id, pool_task_id, content, status, target_date) "
                "VALUES (?, ?, ?, 0, CURDATE())",
                userId, poolTaskId, content
            );

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Task imported to My Day successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[MyDay] Error importing task: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }
};

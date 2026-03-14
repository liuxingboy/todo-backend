#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

using namespace drogon;

static HttpResponsePtr createErrorResp(int code, const std::string& msg) {
    Json::Value ret;
    ret["code"] = code;
    ret["msg"] = msg;
    return HttpResponse::newHttpJsonResponse(ret);
}

class NotesController : public HttpController<NotesController> {
public:
    METHOD_LIST_BEGIN
    // 全部挂载 AuthFilter 门神
    ADD_METHOD_TO(NotesController::getNotes, "/api/notes", Get, "AuthFilter");
    ADD_METHOD_TO(NotesController::createNote, "/api/notes", Post, "AuthFilter");
    ADD_METHOD_TO(NotesController::updateNote, "/api/notes", Put, "AuthFilter");
    ADD_METHOD_TO(NotesController::deleteNote, "/api/notes", Delete, "AuthFilter");
    METHOD_LIST_END

    // 1. 获取我的所有便签
    Task<HttpResponsePtr> getNotes(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto db = app().getDbClient();
        
        try {
            // 按更新时间倒序排列，最近改过的在最前面
            auto result = co_await db->execSqlCoro(
                "SELECT id, content, updated_at FROM notes WHERE user_id = ? ORDER BY updated_at DESC", 
                userId
            );
            
            Json::Value data(Json::arrayValue);
            for (auto row : result) {
                Json::Value note;
                note["id"] = row["id"].as<int64_t>();
                note["content"] = row["content"].as<std::string>();
                note["updated_at"] = row["updated_at"].as<std::string>();
                data.append(note);
            }
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["data"] = data;
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[Notes] Error getting notes: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 2. 新建便签
    Task<HttpResponsePtr> createNote(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["content"].isString()) {
            co_return createErrorResp(400, "Missing content");
        }
        std::string content = (*jsonReq)["content"].asString();

        auto db = app().getDbClient();
        try {
            co_await db->execSqlCoro("INSERT INTO notes (user_id, content) VALUES (?, ?)", userId, content);
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Note created successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[Notes] Error creating note: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 3. 修改便签 (严防越权)
    Task<HttpResponsePtr> updateNote(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["note_id"].isInt64() || !(*jsonReq)["content"].isString()) {
            co_return createErrorResp(400, "Missing note_id or content");
        }
        int64_t noteId = (*jsonReq)["note_id"].asInt64();
        std::string content = (*jsonReq)["content"].asString();

        auto db = app().getDbClient();
        try {
            // 必须加上 user_id = ? 条件，防止修改别人的便签
            auto result = co_await db->execSqlCoro(
                "UPDATE notes SET content = ? WHERE id = ? AND user_id = ?", 
                content, noteId, userId
            );
            
            if (result.affectedRows() == 0) {
                co_return createErrorResp(404, "Note not found or permission denied");
            }

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Note updated successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[Notes] Error updating note: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 4. 删除便签 (严防越权)
    Task<HttpResponsePtr> deleteNote(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto jsonReq = req->getJsonObject();

        if (!jsonReq || !(*jsonReq)["note_id"].isInt64()) {
            co_return createErrorResp(400, "Missing note_id");
        }
        int64_t noteId = (*jsonReq)["note_id"].asInt64();

        auto db = app().getDbClient();
        try {
            // 必须加上 user_id = ? 条件
            auto result = co_await db->execSqlCoro("DELETE FROM notes WHERE id = ? AND user_id = ?", noteId, userId);
            
            if (result.affectedRows() == 0) {
                co_return createErrorResp(404, "Note not found or permission denied");
            }

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "Note deleted successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[Notes] Error deleting note: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }
};

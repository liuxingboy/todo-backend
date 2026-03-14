#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>
#include <drogon/MultiPart.h>
#include <trantor/utils/Date.h>
#include <cstdio> // 用于物理删除文件 std::remove

using namespace drogon;

static HttpResponsePtr createErrorResp(int code, const std::string& msg) {
    Json::Value ret;
    ret["code"] = code;
    ret["msg"] = msg;
    return HttpResponse::newHttpJsonResponse(ret);
}

class FileController : public HttpController<FileController> {
public:
    METHOD_LIST_BEGIN
    // 全局共享，但必须是登录用户才能用
    ADD_METHOD_TO(FileController::uploadFile, "/api/files/upload", Post, "AuthFilter");
    ADD_METHOD_TO(FileController::listFiles, "/api/files", Get, "AuthFilter");
    // {1} 代表占位符，接收路径上的 file_id
    ADD_METHOD_TO(FileController::downloadFile, "/api/files/download/{1}", Get, "AuthFilter");
    // {1} 代表占位符，接收路径上的 file_id
    ADD_METHOD_TO(FileController::deleteFile, "/api/files/{1}", Delete, "AuthFilter");
    METHOD_LIST_END

    // 1. 上传文件 (Multipart Form Data)
    Task<HttpResponsePtr> uploadFile(HttpRequestPtr req) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        
        // 使用 Drogon 的分块解析器
        MultiPartParser fileUpload;
        if (fileUpload.parse(req) != 0 || fileUpload.getFiles().empty()) {
            co_return createErrorResp(400, "No file found in the request");
        }

        // 获取第一个上传的文件
        auto &file = fileUpload.getFiles()[0];
        std::string originalName = file.getFileName();
        size_t fileSize = file.fileLength();

        // 为防止不同用户上传同名文件相互覆盖，我们在文件名前加一个时间戳
        std::string saveName = std::to_string(trantor::Date::now().microSecondsSinceEpoch()) + "_" + originalName;
        std::string savePath = "./shared_files_storage/" + saveName;

        try {
            // 保存到磁盘
            file.saveAs(savePath);

            // 记录到数据库
            auto db = app().getDbClient();
            co_await db->execSqlCoro(
                "INSERT INTO shared_files (uploader_id, file_name, file_path, file_size) VALUES (?, ?, ?, ?)", 
                userId, originalName, savePath, fileSize
            );

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "File uploaded successfully";
            ret["data"]["file_name"] = originalName;
            ret["data"]["size"] = (Json::UInt64)fileSize;
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[File] Upload Error: " << e.what();
            co_return createErrorResp(500, "Failed to save file");
        }
    }

    // 2. 获取共享文件大厅的列表
    Task<HttpResponsePtr> listFiles(HttpRequestPtr req) {
        auto db = app().getDbClient();
        try {
            // 连表查询，顺便把上传这个文件的人的名字查出来 (展示用)
            auto result = co_await db->execSqlCoro(
                "SELECT f.id, f.file_name, f.file_size, f.upload_time, u.username "
                "FROM shared_files f JOIN users u ON f.uploader_id = u.id "
                "ORDER BY f.upload_time DESC"
            );
            
            Json::Value data(Json::arrayValue);
            for (auto row : result) {
                Json::Value fileNode;
                fileNode["id"] = row["id"].as<int64_t>();
                fileNode["file_name"] = row["file_name"].as<std::string>();
                fileNode["size"] = row["file_size"].as<int64_t>(); // 字节数
                fileNode["upload_time"] = row["upload_time"].as<std::string>();
                fileNode["uploader"] = row["username"].as<std::string>();
                data.append(fileNode);
            }
            
            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "ok";
            ret["data"] = data;
            co_return HttpResponse::newHttpJsonResponse(ret);
        } catch (const std::exception &e) {
            LOG_ERROR << "[File] Error getting list: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 3. 下载文件
    Task<HttpResponsePtr> downloadFile(HttpRequestPtr req, std::string fileId) {
        auto db = app().getDbClient();
        try {
            auto result = co_await db->execSqlCoro("SELECT file_name, file_path FROM shared_files WHERE id = ?", fileId);
            
            if (result.empty()) {
                co_return createErrorResp(404, "File not found");
            }

            std::string filePath = result[0]["file_path"].as<std::string>();
            std::string fileName = result[0]["file_name"].as<std::string>();

            // 直接让框架作为附件下发，CT_APPLICATION_OCTET_STREAM 告诉浏览器这是一个需要下载的二进制文件
            auto resp = HttpResponse::newFileResponse(filePath, fileName, CT_APPLICATION_OCTET_STREAM);
            co_return resp;

        } catch (const std::exception &e) {
            LOG_ERROR << "[File] Error downloading file: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }

    // 4. 删除文件 (防越权：只能删除自己上传的)
    Task<HttpResponsePtr> deleteFile(HttpRequestPtr req, std::string fileId) {
        auto userId = req->attributes()->get<int64_t>("user_id");
        auto db = app().getDbClient();

        try {
            // 第一步：先查出文件物理路径，并严防越权（必须是 uploader_id = 当前用户）
            auto checkResult = co_await db->execSqlCoro(
                "SELECT file_path FROM shared_files WHERE id = ? AND uploader_id = ?",
                fileId, userId
            );

            if (checkResult.empty()) {
                co_return createErrorResp(403, "File not found or permission denied (You can only delete your own files)");
            }

            std::string filePath = checkResult[0]["file_path"].as<std::string>();

            // 第二步：斩草除根，删除硬盘上的物理二进制文件
            if (std::remove(filePath.c_str()) != 0) {
                // 如果物理文件由于某种原因（比如被人为手动删了）不存在，打印个日志，但业务继续往下走
                LOG_WARN << "[File] Physical file not found or could not be deleted: " << filePath;
            }

            // 第三步：删除数据库里的记录
            co_await db->execSqlCoro("DELETE FROM shared_files WHERE id = ?", fileId);

            Json::Value ret;
            ret["code"] = 0;
            ret["msg"] = "File deleted successfully";
            co_return HttpResponse::newHttpJsonResponse(ret);

        } catch (const std::exception &e) {
            LOG_ERROR << "[File] Error deleting file: " << e.what();
            co_return createErrorResp(500, "Database Error");
        }
    }
};

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

using namespace drogon;

class DonateController : public HttpController<DonateController> {
public:
    METHOD_LIST_BEGIN
    // 打赏二维码我们做成公开接口，不需要挂载 "AuthFilter"，方便任何人扫码
    ADD_METHOD_TO(DonateController::getDonateQr, "/api/donate/qr", Get);
    METHOD_LIST_END

    Task<HttpResponsePtr> getDonateQr(HttpRequestPtr req) {
        // 直接利用 Drogon 内置的文件响应引擎
        // 它会自动识别是 png 还是 jpg，并自动加上正确的 Content-Type (如 image/png)
        // 假设图片放在运行目录 (build) 下的 assets 文件夹中
        auto resp = HttpResponse::newFileResponse("./assets/donate.png");
        
        co_return resp;
    }
};

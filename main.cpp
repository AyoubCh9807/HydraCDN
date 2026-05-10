#include <drogon/HttpRequest.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <glaze/glaze.hpp>
#include <glaze/json/write.hpp>
#include <spdlog/spdlog.h>

using RouteCallback = std::function<void(const drogon::HttpResponsePtr &)>;

struct Dragon {
  std::string id;
  std::string name;
  int age;
  struct glz {
    using T = Dragon;
    static constexpr auto value =
        ::glz::object("id", &T::id, "name", &T::name, "age", &T::age);
  };
};

int main() {
  // 1. Setup Logging
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  spdlog::info("Starting UltraBackend Prod Stack...");

  drogon::app().registerHandler(
      "/generate",
      [](const drogon::HttpRequestPtr &req, RouteCallback &&callback) {
        auto res = drogon::HttpResponse::newHttpResponse();

        Dragon data = {.id = "123", .name = "Arshadow", .age = 400};

        std::string buffer = ::glz::write_json(data);

        if (buffer.empty()) {
          res->setContentTypeCode(drogon::CT_APPLICATION_JSON);
          res->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
          callback(res);
          return;
        }

        res->setBody(buffer);
        res->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        res->setStatusCode(drogon::HttpStatusCode::k200OK);

        callback(res);
      },
      {drogon::Post});

  // ... rest of your code ...
}

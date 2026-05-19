#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/MultiPart.h>
#include <drogon/drogon.h> 
#include <expected>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <tbb/concurrent_hash_map.h>
#include <vips/vips8>

using RouteCallback = std::function<void(const drogon::HttpResponsePtr &)>;
using ImageCache = tbb::detail::d2::concurrent_hash_map<std::string, std::vector<char>>;

enum class CacheError { NotFound, Expired, Corrupted };

static ImageCache global_image_cache;

std::expected<std::string_view, CacheError> getFromCache(std::string_view id, ImageCache::const_accessor &acc);
void writeToCache(std::string_view id, const void *buf, size_t len);

int main(int argc, char **argv) {
  if (VIPS_INIT(argv[0])) {
    vips_error_exit("Unable to start libvips");
    spdlog::error("Cannot initialize libvips");
    return 1;
  }

  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  spdlog::info("Starting UltraBackend Prod Stack...");

  drogon::app().registerHandler(
      "/extract",
      [](const drogon::HttpRequestPtr &req, RouteCallback &&callback) {
        auto res = drogon::HttpResponse::newHttpResponse();
        auto const params = req->parameters();

        if (!params.contains("width") || !params.contains("height") ||
            !params.contains("top") || !params.contains("left") ||
            !params.contains("id")) {
          res->setStatusCode(drogon::k400BadRequest);
          res->setBody("Missing dimensions (width, height, top, left)");
          callback(res);
          return;
        }

        ImageCache::const_accessor read_accessor;
        std::string_view id = params.at("id");
        auto cache_item = getFromCache(id, read_accessor);
        if (cache_item.has_value()) {
          res->setBody(std::string(cache_item.value()));
          res->setContentTypeCode(drogon::CT_IMAGE_WEBP);
          read_accessor.release();
          callback(res);
          return;
        }
        read_accessor.release();

        try {
          drogon::MultiPartParser parser;
          if (parser.parse(req) != 0 || parser.getFiles().empty()) {
            res->setStatusCode(drogon::k400BadRequest);
            res->setBody("No image file found in request");
            callback(res);
            return;
          }

          auto file = parser.getFiles()[0];
          vips::VImage in = vips::VImage::new_from_buffer(
              file.fileContent().data(), file.fileLength(), "");

          int top = std::stoi(params.at("top"));
          int left = std::stoi(params.at("left"));
          int width = std::stoi(params.at("width"));
          int height = std::stoi(params.at("height"));

          vips::VImage out = in.extract_area(left, top, width, height);

          void *buf = nullptr;
          size_t len = 0;
          out.write_to_buffer(".webp[Q=80]", &buf, &len);

          writeToCache(id, buf, len);

          res->setBody(std::string(static_cast<char *>(buf), len));
          res->setContentTypeCode(drogon::CT_IMAGE_WEBP);

          g_free(buf);
          callback(res);
          return;
        } catch (const vips::VError &e) {
          res->setStatusCode(drogon::k500InternalServerError);
          res->setBody(std::string("Vips Error: ") + e.what());
          spdlog::error("Libvips exception happened at /extract: {}", e.what());
          callback(res);
        } catch (const std::exception &e) {
          spdlog::error("Exception happened at /extract: {}", e.what());
          res->setStatusCode(drogon::k500InternalServerError);
          res->setBody(std::string("Error: ") + e.what());
          callback(res);
        }
      });

  drogon::app().registerHandler(
      "/extract_and_fit", 
      [](const drogon::HttpRequestPtr &req, RouteCallback &&callback) {
        drogon::MultiPartParser parser;
        auto res = drogon::HttpResponse::newHttpResponse();
        if (parser.parse(req) != 0 || parser.getFiles().empty()) {
          res->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
          res->setBody("Error: File missing! Make sure to include the file you want to resize");
          callback(res);
          return;
        }

        const auto params = req->parameters();

        if (!params.contains("width") || !params.contains("height") ||
            !params.contains("top") || !params.contains("left") ||
            !params.contains("scale_width") || !params.contains("scale_height") ||
            !params.contains("id")) {
          res->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
          res->setBody("Error: Missing dimensions or transform configuration parameters");
          callback(res);
          return;
        }

        try {
          const int top = std::stoi(params.at("top"));
          const int left = std::stoi(params.at("left"));
          const int width = std::stoi(params.at("width"));
          const int height = std::stoi(params.at("height"));
          const int scale_width = std::stoi(params.at("scale_width"));
          const int scale_height = std::stoi(params.at("scale_height"));
          std::string_view id = params.at("id");

          if (width < 0 || height < 0 || top < 0 || left < 0 || scale_width < 0 ||
              scale_height < 0 || id.empty()) {
            res->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
            res->setBody("Error: Invalid dimensions received! Make sure the dimensions are valid");
            callback(res);
            return;
          }

          ImageCache::const_accessor read_accessor;
          auto cache_item = getFromCache(id, read_accessor);
          if (cache_item.has_value()) {
            res->setBody(std::string(cache_item.value()));
            res->setContentTypeCode(drogon::CT_IMAGE_WEBP);
            read_accessor.release();
            callback(res);
            return;
          }
          read_accessor.release();

          const auto file = parser.getFiles()[0];
          vips::VImage in = vips::VImage::new_from_buffer(file.fileContent().data(), file.fileLength(), "");
          vips::VImage out = in.extract_area(left, top, width, height);

          if (scale_width != width || scale_height != height) {
            const double scale_x = static_cast<double>(scale_width) / width;
            const double scale_y = static_cast<double>(scale_height) / height;
            out = out.resize(scale_x, vips::VImage::option()->set("vscale", scale_y));
          }

          void *buf = nullptr;
          size_t len = 0;
          out.write_to_buffer(".webp[Q=80]", &buf, &len);
          
          writeToCache(id, buf, len);
          
          res->setStatusCode(drogon::HttpStatusCode::k200OK);
          res->setBody(std::string(static_cast<char *>(buf), len));
          g_free(buf);

          callback(res);
          return;
        } catch (std::exception &e) {
          spdlog::error("Exception happened at /extract_and_fit: {}", e.what());
          res->setStatusCode(drogon::HttpStatusCode::k500InternalServerError);
          res->setBody(std::string("Error: ") + e.what());
          callback(res);
        }
      });

  drogon::app().addListener("0.0.0.0", 8080).run();
  vips_shutdown();
  return 0;
}

std::string formatError(CacheError err) {
  switch (err) {
    case CacheError::Expired: return "Error: Cache item expired!";
    case CacheError::NotFound: return "Error: Cache item not found!";
    case CacheError::Corrupted: return "Error: Cache corrupted!";
  }
}

std::expected<std::string_view, CacheError> getFromCache(std::string_view id, ImageCache::const_accessor &acc) {
  if (global_image_cache.find(acc, std::string(id))) {
    const auto &bytes = acc->second;
    return std::string_view(bytes.data(), bytes.size());
  }
  return std::unexpected(CacheError::NotFound);
}

void writeToCache(std::string_view id, const void *buf, size_t len) {
  if (!buf) return;

  static std::atomic<size_t> write_counter{0};
  size_t current_count = write_counter.fetch_add(1, std::memory_order_relaxed);

  // Hard limit memory growth to 1000 entries
  if (global_image_cache.size() >= 1000) {
    // Probabilistic eviction to prevent stalling under load
    if (current_count % 10 == 0) {
      global_image_cache.erase(std::string(id));
    }
    return; 
  }

  ImageCache::accessor acc;
  if (global_image_cache.insert(acc, std::string(id))) {
    acc->second.assign(static_cast<const char *>(buf), static_cast<const char *>(buf) + len);
  }
}

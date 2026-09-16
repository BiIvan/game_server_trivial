#pragma once

#include <cctype>
#include <string>
#include <utility>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <boost/json.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/system/error_code.hpp>
#include <boost/beast/http/file_body.hpp>

#include "model.h"

namespace http_handler {

  namespace fs = std::filesystem;
  namespace beast = boost::beast;
  namespace sys = boost::system;
  namespace http = beast::http;
  namespace json = boost::json;

  class RequestHandler {
    using StringResponse = http::response<http::string_body>;
    using EmptyResponse = http::response<http::empty_body>;
    using FileResponse = http::response<http::file_body>;

    model::Game& game_;
    fs::path static_root_;

    static bool StartsWith( beast::string_view value, beast::string_view prefix) {
      return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    static std::string ToLower(std::string value) {
      std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
          return static_cast<char>(std::tolower(ch));
        }
      );
      return value;
    }

    static std::string GetMimeType(const fs::path& path) {
      const std::string ext = ToLower(path.extension().string());
      if (ext == ".htm" || ext == ".html") return "text/html";
      if (ext == ".css") return "text/css";
      if (ext == ".txt") return "text/plain";
      if (ext == ".js") return "text/javascript";
      if (ext == ".json") return "application/json";
      if (ext == ".xml") return "application/xml";
      if (ext == ".png") return "image/png";
      if (ext == ".jpg" || ext == ".jpe" || ext == ".jpeg") return "image/jpeg";
      if (ext == ".gif") return "image/gif";
      if (ext == ".bmp") return "image/bmp";
      if (ext == ".ico") return "image/vnd.microsoft.icon";
      if (ext == ".tiff" || ext == ".tif") return "image/tiff";
      if (ext == ".svg" || ext == ".svgz") return "image/svg+xml";
      if (ext == ".mp3") return "audio/mpeg";
      return "application/octet-stream";
    }

    static std::optional<std::string> UrlDecode(
      beast::string_view encoded) {
      auto hex_to_int = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') { return ch - '0'; }
        if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
        if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
        return -1;
      };
      std::string decoded;
      decoded.reserve(encoded.size());
      for (size_t i = 0; i < encoded.size(); ++i) {
        const char ch = encoded[i];
        if (ch != '%') {
          decoded.push_back(ch);
          continue;
        }
        if (i + 2 >= encoded.size()) { return std::nullopt; }
        const int high = hex_to_int(encoded[i + 1]);
        const int low = hex_to_int(encoded[i + 2]);
        if (high < 0 || low < 0) { return std::nullopt; }
        decoded.push_back( static_cast<char>((high << 4) | low) );
        i += 2;
      }
      return decoded;
    }

    static bool IsSubPath( const fs::path& path, const fs::path& base) {
      const fs::path normalized_path = fs::weakly_canonical(path);
      const fs::path normalized_base = fs::weakly_canonical(base);
      auto path_it = normalized_path.begin();
      for (auto base_it = normalized_base.begin();
         base_it != normalized_base.end();
         ++base_it, ++path_it) {
        if (path_it == normalized_path.end() ||
          *path_it != *base_it) {
          return false;
        }
      }
      return true;
    }

    static StringResponse MakeTextResponse(
      http::status status,
      unsigned version,
      bool keep_alive,
      std::string body) {
      StringResponse response{status, version};
      response.set(http::field::content_type, "text/plain");
      response.keep_alive(keep_alive);
      response.body() = std::move(body);
      response.prepare_payload();
      return response;
    }

    static StringResponse MakeJsonResponse(
      http::status status,
      unsigned version,
      bool keep_alive,
      json::value body) {
      StringResponse response{status, version};
      response.set(http::field::content_type, "application/json");
      response.keep_alive(keep_alive);
      response.body() = json::serialize(body);
      response.prepare_payload();
      return response;
    }

    static StringResponse MakeErrorResponse(
      http::status status,
      unsigned version,
      bool keep_alive,
      std::string_view code,
      std::string_view message) {
      return MakeJsonResponse(
        status, version, keep_alive,
        json::object{
          {"code", code},
          {"message", message},
        }
      );
    }

    static json::object SerializeRoad(const model::Road& road) {
      const model::Point start = road.GetStart();
      const model::Point end = road.GetEnd();
      if (road.IsHorizontal()) {
        return { {"x0", start.x}, {"y0", start.y}, {"x1", end.x}, };
      }
      return { {"x0", start.x}, {"y0", start.y}, {"y1", end.y}, };
    }

    static json::object SerializeBuilding(
      const model::Building& building) {
      const model::Rectangle& bounds = building.GetBounds();
      return {
        {"x", bounds.position.x}, {"y", bounds.position.y}, 
        {"w", bounds.size.width}, {"h", bounds.size.height},
      };
    }

    static json::object SerializeOffice(const model::Office& office) {
        const model::Point position = office.GetPosition();
        const model::Offset offset = office.GetOffset();
        return {
            {"id", *office.GetId()}, {"x", position.x}, {"y", position.y},
            {"offsetX", offset.dx}, {"offsetY", offset.dy},
        };
    }

    static json::object SerializeMap(const model::Map& map) {
        json::array roads;
        roads.reserve(map.GetRoads().size());
        for (const model::Road& road : map.GetRoads()) {
            roads.emplace_back(SerializeRoad(road));
        }
        json::array buildings;
        buildings.reserve(map.GetBuildings().size());
        for (const model::Building& building : map.GetBuildings()) {
            buildings.emplace_back(SerializeBuilding(building));
        }
        json::array offices;
        offices.reserve(map.GetOffices().size());
        for (const model::Office& office : map.GetOffices()) {
            offices.emplace_back(SerializeOffice(office));
        }
        return {
            {"id", *map.GetId()}, {"name", map.GetName()}, {"roads", std::move(roads)},
            {"buildings", std::move(buildings)}, {"offices", std::move(offices)},
        };
    }

    StringResponse MakeMapsResponse(
        unsigned version,
        bool keep_alive) const {
        json::array maps;
        maps.reserve(game_.GetMaps().size());
        for (const model::Map& map : game_.GetMaps()) {
            maps.emplace_back(json::object{
                {"id", *map.GetId()},
                {"name", map.GetName()},
            });
        }
        return MakeJsonResponse(
            http::status::ok,
            version,
            keep_alive,
            std::move(maps)
        );
    }

    static StringResponse MakeMapResponse(
        const model::Map& map,
        unsigned version,
        bool keep_alive) {
        return MakeJsonResponse(
            http::status::ok,
            version,
            keep_alive,
            SerializeMap(map)
        );
    }

    template <typename Send>
    void HandleApiRequest(
        http::request<http::string_body>&& req,
        Send&& send) const {
        if (req.method() != http::verb::get) {
            send(MakeErrorResponse(
                http::status::bad_request,
                req.version(),
                req.keep_alive(),
                "badRequest",
                "Bad request"
            ));
            return;
        }
        const std::string target{req.target()};
        if (target == "/api/v1/maps") {
            send(MakeMapsResponse(
                req.version(),
                req.keep_alive()
            ));
            return;
        }
        constexpr std::string_view maps_prefix =
            "/api/v1/maps/";
        const bool is_map_request =
            target.size() >= maps_prefix.size() &&
            target.compare(
                0,
                maps_prefix.size(),
                maps_prefix
            ) == 0;
        if (is_map_request) {
            const std::string_view map_id =
                std::string_view{target}.substr(
                    maps_prefix.size()
                );
            if (map_id.empty() ||
                map_id.find('/') != std::string_view::npos) {
                send(MakeErrorResponse(
                    http::status::bad_request,
                    req.version(),
                    req.keep_alive(),
                    "badRequest",
                    "Bad request"
                ));
                return;
            }
            const model::Map* map = game_.FindMap(
                model::Map::Id{std::string(map_id)}
            );
            if (map == nullptr) {
                send(MakeErrorResponse(
                    http::status::not_found,
                    req.version(),
                    req.keep_alive(),
                    "mapNotFound",
                    "Map not found"
                ));
                return;
            }
            send(MakeMapResponse(
                *map,
                req.version(),
                req.keep_alive()
            ));
            return;
        }
        send(MakeErrorResponse(
            http::status::bad_request,
            req.version(),
            req.keep_alive(),
            "badRequest",
            "Bad request"
        ));
    }

    template <typename Send>
    void HandleStaticRequest(
        http::request<http::string_body>&& req,
        Send&& send) const {
        if (req.method() != http::verb::get &&
            req.method() != http::verb::head) {
            auto response = MakeTextResponse(
                http::status::method_not_allowed,
                req.version(),
                req.keep_alive(),
                "Only GET and HEAD methods are supported\n"
            );
            response.set(http::field::allow, "GET, HEAD");
            send(std::move(response));
            return;
        }
        const beast::string_view target = req.target();
        if (target.empty() || target.front() != '/') {
            send(MakeTextResponse(
                http::status::bad_request,
                req.version(),
                req.keep_alive(),
                "Invalid request target\n"
            ));
            return;
        }
        const size_t query_position = target.find('?');
        const beast::string_view encoded_path =
            target.substr(0, query_position);
        const std::optional<std::string> decoded_path =
            UrlDecode(encoded_path);
        if (!decoded_path) {
            send(MakeTextResponse(
                http::status::bad_request,
                req.version(),
                req.keep_alive(),
                "Invalid URL encoding\n"
            ));
            return;
        }
        std::string relative_path = *decoded_path;
        relative_path.erase(0, 1);
        fs::path requested_path =
            fs::weakly_canonical(
                static_root_ / relative_path
            );
        if (!IsSubPath(requested_path, static_root_)) {
            send(MakeTextResponse(
                http::status::bad_request,
                req.version(),
                req.keep_alive(),
                "Requested path is outside static directory\n"
            ));
            return;
        }
        sys::error_code ec;
        const bool is_directory =
            fs::is_directory(requested_path, ec);
        if (ec) {
            send(MakeTextResponse(
                http::status::not_found,
                req.version(),
                req.keep_alive(),
                "File not found\n"
            ));
            return;
        }
        if (is_directory) {
            requested_path /= "index.html";
            requested_path =
                fs::weakly_canonical(requested_path);
        }
        if (!IsSubPath(requested_path, static_root_)) {
            send(MakeTextResponse(
                http::status::bad_request,
                req.version(),
                req.keep_alive(),
                "Requested path is outside static directory\n"
            ));
            return;
        }
        http::file_body::value_type file;
        file.open(
            requested_path.string().c_str(),
            beast::file_mode::read, ec
        );
        if (ec == beast::errc::no_such_file_or_directory) {
            send(MakeTextResponse(
                http::status::not_found, req.version(),
                req.keep_alive(), "File not found\n"
            ));
            return;
        }
        if (ec) {
            send(MakeTextResponse(
                http::status::internal_server_error,
                req.version(), req.keep_alive(), "Failed to open file\n"
            ));
            return;
        }
        const auto file_size = file.size();
        if (req.method() == http::verb::head) {
            EmptyResponse response{
                http::status::ok,
                req.version()
            };
            response.set(
                http::field::content_type,
                GetMimeType(requested_path)
            );
            response.content_length(file_size);
            response.keep_alive(req.keep_alive());
            send(std::move(response));
            return;
        }
        FileResponse response{
            std::piecewise_construct,
            std::make_tuple(std::move(file)),
            std::make_tuple( http::status::ok, req.version() )
        };
        response.set(
            http::field::content_type,
            GetMimeType(requested_path)
        );
        response.content_length(file_size);
        response.keep_alive(req.keep_alive());
        send(std::move(response));
    }

  public:
    RequestHandler( model::Game& game, fs::path static_root)
      : game_{game}
      , static_root_{ fs::weakly_canonical(fs::absolute(std::move(static_root)))} {
    }
    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;
    template <typename Body, typename Allocator, typename Send>
    void operator()( http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) const {
      http::request<http::string_body> string_request{ std::move(req) };
      const beast::string_view target = string_request.target();
      if (StartsWith(target, "/api/") || target == "/api") {
        HandleApiRequest( std::move(string_request), std::forward<Send>(send));
        return;
      }
      HandleStaticRequest( std::move(string_request), std::forward<Send>(send));
    }
  };

}  // namespace http_handler

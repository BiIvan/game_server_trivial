#pragma once

#include <cctype>
#include <string>
#include <cassert>
#include <utility>
#include <variant>
#include <iostream>
#include <optional>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <string_view>
#include <boost/json.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/dispatch.hpp>

#include "model.h"
#include "application.h"

namespace http_handler {
  
  namespace net = boost::asio;
  namespace beast = boost::beast;
  namespace http = beast::http;
  namespace json = boost::json;
  namespace fs = std::filesystem;
  
  using tcp = net::ip::tcp;
  using Strand = net::strand<net::io_context::executor_type>;
  
  class RequestHandler {
    using StringRequest = http::request<http::string_body>;
    using StringResponse = http::response<http::string_body>;
    using EmptyResponse = http::response<http::empty_body>;
    using FileResponse = http::response<http::file_body>;
    using FileRequestResult = std::variant<
      StringResponse,
      EmptyResponse,
      FileResponse>;
      
    FileRequestResult HandleFileRequest( const StringRequest& req) const;
    StringResponse HandleApiRequest( const StringRequest& request);
    StringResponse HandleJoinGameRequest( const StringRequest& request);
    StringResponse HandlePlayersRequest( const StringRequest& request);
    StringResponse ReportServerError( unsigned version, bool keep_alive) const;
    
    model::Game& game_;
    app::Application app_;
    fs::path static_root_;
    Strand api_strand_;
    
    template <typename Body, typename Allocator>
    static bool IsApiRequest(
      const http::request<Body, http::basic_fields<Allocator>>& request) {
      constexpr std::string_view kApiPrefix = "/api/";
      const std::string_view target{
          request.target().data(),
          request.target().size()
      };
      return target.starts_with(kApiPrefix);
    }

    static std::string ToLower(std::string value) {
      std::transform(
          value.begin(),
          value.end(),
          value.begin(),
          [](unsigned char ch) {
              return static_cast<char>(std::tolower(ch));
          });
      return value;
    }

    static std::string GetMimeType(const fs::path& path) {
      const std::string extension = ToLower(path.extension().string());
      if (extension == ".htm" || extension == ".html") { return "text/html"; }
      if (extension == ".css") { return "text/css"; }
      if (extension == ".txt") { return "text/plain"; }
      if (extension == ".js") { return "text/javascript"; }
      if (extension == ".json") { return "application/json"; }
      if (extension == ".xml") { return "application/xml"; }
      if (extension == ".png") { return "image/png"; }
      if (extension == ".jpg" || extension == ".jpeg" ||
          extension == ".jpe") { return "image/jpeg"; }
      if (extension == ".gif") { return "image/gif"; }
      if (extension == ".bmp") { return "image/bmp"; }
      if (extension == ".ico") { return "image/vnd.microsoft.icon"; }
      if (extension == ".tiff" || extension == ".tif") { return "image/tiff"; }
      if (extension == ".svg" || extension == ".svgz") { return "image/svg+xml"; }
      if (extension == ".mp3") { return "audio/mpeg"; }
      return "application/octet-stream";
    }
    
    static std::optional<std::string> UrlDecode(
      beast::string_view encoded) {
      const auto hex_to_int = [](char ch) -> int {
          if (ch >= '0' && ch <= '9') {
              return ch - '0';
          }
          if (ch >= 'a' && ch <= 'f') {
              return ch - 'a' + 10;
          }
          if (ch >= 'A' && ch <= 'F') {
              return ch - 'A' + 10;
          }
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
          if (i + 2 >= encoded.size()) {
              return std::nullopt;
          }
          const int high = hex_to_int(encoded[i + 1]);
          const int low = hex_to_int(encoded[i + 2]);
          if (high < 0 || low < 0) {
              return std::nullopt;
          }
          decoded.push_back(
              static_cast<char>((high << 4) | low));
          i += 2;
      }
      return decoded;
    }
    
    static bool IsSubPath(
      const fs::path& path,
      const fs::path& base) {
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
      response.set(http::field::cache_control, "no-cache");
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
          status,
          version,
          keep_alive,
          json::object{
              {"code", code},
              {"message", message},
          });
    }
    
    static bool IsJsonContentType(
      const StringRequest& request) {
      const auto content_type = request[http::field::content_type];
      const size_t semicolon = content_type.find(';');
      const auto media_type = content_type.substr(0, semicolon);
      return beast::iequals(media_type, "application/json");
    }
    
    static std::optional<model::Token> ExtractBearerToken(
      const StringRequest& request);
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
          {"x", bounds.position.x},
          {"y", bounds.position.y},
          {"w", bounds.size.width},
          {"h", bounds.size.height},
      };
    }
    
    static json::object SerializeOffice(
      const model::Office& office) {
      const model::Point position = office.GetPosition();
      const model::Offset offset = office.GetOffset();
      return {
          {"id", *office.GetId()},
          {"x", position.x},
          {"y", position.y},
          {"offsetX", offset.dx},
          {"offsetY", offset.dy},
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
          {"id", *map.GetId()},
          {"name", map.GetName()},
          {"roads", std::move(roads)},
          {"buildings", std::move(buildings)},
          {"offices", std::move(offices)},
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
          std::move(maps));
    }
    
    static StringResponse MakeMapResponse(
      const model::Map& map,
      unsigned version,
      bool keep_alive) {
      return MakeJsonResponse(
          http::status::ok,
          version,
          keep_alive,
          SerializeMap(map));
    }
      
  public:
    explicit RequestHandler(
        model::Game& game,
        fs::path static_root,
        Strand api_strand)
        : game_(game)
        , app_(game_)
        , static_root_{
              fs::weakly_canonical(fs::absolute(std::move(static_root)))}
        , api_strand_(std::move(api_strand)) {
    }
    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;
    template <typename Body, typename Allocator, typename Send>
    void operator()(
        http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        const unsigned version = req.version();
        const bool keep_alive = req.keep_alive();
        try {
          if (IsApiRequest(req)) {
            auto handle = [
              this,
              req = std::move(req),
              send = std::forward<Send>(send),
              version,
              keep_alive
            ]() mutable {
              try {
                http::request<http::string_body> string_request{
                  std::move(req)
                };
                send(HandleApiRequest(string_request));
              } catch (const std::exception& ex) {
                std::cerr << "API handler error: " << ex.what() << '\n';
                send(ReportServerError(
                  version,
                  keep_alive));
              } catch (...) {
                std::cerr << "API handler error: unknown exception\n";
                send(ReportServerError(
                  version,
                  keep_alive));
              }
            };
            net::post(api_strand_, std::move(handle));
            return;
          }
          http::request<http::string_body> string_request{
            std::move(req)};
          std::visit(
            [&send](auto&& result) {
              send(std::forward<decltype(result)>(result));
            },
            HandleFileRequest(string_request));
        } catch (...) {
          send(ReportServerError(version, keep_alive));
        }
    }
  };
}  // namespace http_handler
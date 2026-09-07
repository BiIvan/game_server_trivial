#pragma once

#include <string>
#include <utility>
#include <string_view>
#include <boost/json.hpp>

#include "model.h"
#include "http_server.h"

namespace http_handler {

  namespace beast = boost::beast;
  namespace http = beast::http;
  namespace json = boost::json;

  class RequestHandler {
    using StringResponse = http::response<http::string_body>;

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
        status,
        version,
        keep_alive,
        json::object{
          {"code", code},
          {"message", message},
        });
    }

    static json::object SerializeRoad(const model::Road& road) {
      const model::Point start = road.GetStart();
      const model::Point end = road.GetEnd();
      if (road.IsHorizontal()) {
        return {
          {"x0", start.x},
          {"y0", start.y},
          {"x1", end.x},
        };
      }
      return {
        {"x0", start.x},
        {"y0", start.y},
        {"y1", end.y},
      };
    }

    static json::object SerializeBuilding(const model::Building& building) {
      const model::Rectangle& bounds = building.GetBounds();
      return {
        {"x", bounds.position.x},
        {"y", bounds.position.y},
        {"w", bounds.size.width},
        {"h", bounds.size.height},
      };
    }

    static json::object SerializeOffice(const model::Office& office) {
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

    StringResponse MakeMapsResponse(unsigned version, bool keep_alive) const {
      json::array maps;
      maps.reserve(game_.GetMaps().size());
      for (const model::Map& map : game_.GetMaps()) {
        maps.emplace_back(json::object{{"id", *map.GetId()},{"name", map.GetName()},});
      }
      return MakeJsonResponse(http::status::ok,version,keep_alive,std::move(maps));
    }

    static StringResponse MakeMapResponse(const model::Map& map,unsigned version,bool keep_alive) {
      return MakeJsonResponse(http::status::ok,version,keep_alive,SerializeMap(map));
    }

    model::Game& game_;
    
  public:
    explicit RequestHandler(model::Game& game)
      : game_{game} {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Send>
    void operator()(http::request<http::string_body>&& req, Send&& send) {
      if (req.method() != http::verb::get) {
        return send(MakeErrorResponse(
          http::status::bad_request,
          req.version(),
          req.keep_alive(),
          "badRequest",
          "Bad request"));
      }
      const std::string target{req.target()};
      if (target == "/api/v1/maps") {
        return send(MakeMapsResponse(req.version(), req.keep_alive()));
      }
      constexpr std::string_view maps_prefix = "/api/v1/maps/";
      if (target.starts_with(maps_prefix)) {
        const std::string_view map_id = target.substr(maps_prefix.size());
        if (map_id.empty() || map_id.find('/') != std::string_view::npos) {
          return send(MakeErrorResponse(
            http::status::bad_request,
            req.version(),
            req.keep_alive(),
            "badRequest",
            "Bad request"));
        }
        const model::Map* map{game_.FindMap(model::Map::Id{std::string(map_id)})};
        if (!map) {
          return send(MakeErrorResponse(
            http::status::not_found,
            req.version(),
            req.keep_alive(),
            "mapNotFound",
            "Map not found"));
        }
        return send(MakeMapResponse(*map,req.version(),req.keep_alive()));
      }
      return send(MakeErrorResponse(
        http::status::bad_request,
        req.version(),
        req.keep_alive(),
        "badRequest",
        "Bad request"));
    }
  };

}  // namespace http_handler
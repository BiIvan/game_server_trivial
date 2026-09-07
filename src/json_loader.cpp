#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <boost/json.hpp>

#include "json_loader.h"

namespace json_loader {  

  namespace json = boost::json;
  
  namespace {
    
    int GetInt(const json::object& object, std::string_view key) {
        return static_cast<int>(object.at(key).as_int64());
    }

    std::string GetString(const json::object& object, std::string_view key) {
        return json::value_to<std::string>(object.at(key));
    }

    model::Road ParseRoad(const json::object& road) {
      const model::Point start{ GetInt(road, "x0"), GetInt(road, "y0")};
      if (road.if_contains("x1")) {
        return model::Road{ model::Road::HORIZONTAL, start, GetInt(road, "x1")};
      }
      if (road.if_contains("y1")) {
        return model::Road{ model::Road::VERTICAL, start, GetInt(road, "y1")};
      }
      throw std::invalid_argument("Road must contain x1 or y1");
    }

    model::Building ParseBuilding(const json::object& building) {
      return model::Building{
        model::Rectangle{
          .position = {GetInt(building, "x"),GetInt(building, "y")},
          .size = {GetInt(building, "w"),GetInt(building, "h")}
        }
      };
    }

    model::Office ParseOffice(const json::object& office) {
      return model::Office{
        model::Office::Id{GetString(office, "id")},
        model::Point{GetInt(office, "x"),GetInt(office, "y")},
        model::Offset{GetInt(office, "offsetX"),GetInt(office, "offsetY")},
      };
    }

    model::Map ParseMap(const json::object& map_json) {
      model::Map map{
        model::Map::Id{GetString(map_json, "id")},GetString(map_json, "name")};
      for (const json::value& value : map_json.at("roads").as_array()) {
        map.AddRoad(ParseRoad(value.as_object()));
      }
      for (const json::value& value : map_json.at("buildings").as_array()) {
        map.AddBuilding(ParseBuilding(value.as_object()));
      }
      for (const json::value& value : map_json.at("offices").as_array()) {
        map.AddOffice(ParseOffice(value.as_object()));
      }
      return map;
    }

  }  // namespace

  model::Game LoadGame(const std::filesystem::path& json_path) {
    std::ifstream input{json_path};
    if (!input) {
      throw std::runtime_error("Cannot open config file: " + json_path.string());
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const json::value root_value = json::parse(buffer.str());
    const json::object& root = root_value.as_object();
    model::Game game;
    for (const json::value& value : root.at("maps").as_array()) {
      game.AddMap(ParseMap(value.as_object()));
    }
    return game;
  }

}  // namespace json_loader
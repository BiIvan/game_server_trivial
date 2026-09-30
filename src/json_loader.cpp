#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <boost/json.hpp>

#include "literals.h"
#include "json_loader.h"

using MAP = model::Map;

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
      const model::Point start{ GetInt(road, X0), GetInt(road, Y0)};
      if (road.if_contains(X1)) {
        return model::Road{ model::Road::HORIZONTAL, start, GetInt(road, X1)};
      }
      if (road.if_contains(Y1)) {
        return model::Road{ model::Road::VERTICAL, start, GetInt(road, Y1)};
      }
      throw std::invalid_argument(std::string{ROADERR});
    }

    model::Building ParseBuilding(const json::object& building) {
      return model::Building{
        model::Rectangle{
          .position = {GetInt(building, X),GetInt(building, Y)},
          .size = {GetInt(building, W),GetInt(building, H)}
        }
      };
    }

    model::Office ParseOffice(const json::object& office) {
      return model::Office{
        model::Office::Id{GetString(office, ID)},
        model::Point{GetInt(office, X),GetInt(office, Y)},
        model::Offset{GetInt(office, OffX),GetInt(office, OffY)},
      };
    }

    MAP ParseMap( const json::object& map_json, double default_dog_speed) {
      const json::value* configured_speed = map_json.if_contains(SPEEDOG);
      const double dog_speed = configured_speed
        ? json::value_to<double>(*configured_speed)
        : default_dog_speed;
      MAP map{ MAP::Id{GetString(map_json, ID)}, GetString(map_json, NAME), dog_speed};
      for (const json::value& value : map_json.at(ROAD).as_array()) {
        map.AddRoad(ParseRoad(value.as_object()));
      }
      for (const json::value& value : map_json.at(BUILD).as_array()) {
        map.AddBuilding(ParseBuilding(value.as_object()));
      }
      for (const json::value& value : map_json.at(OFFICE).as_array()) {
        map.AddOffice(ParseOffice(value.as_object()));
      }
      return map;
    }

  }  // namespace

  model::Game LoadGame(const std::filesystem::path& json_path) {
    std::ifstream input{json_path};
    if (!input) {
      throw std::runtime_error(std::string{NOCONFIG} + json_path.string());
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const json::value root_value = json::parse(buffer.str());
    const json::object& root = root_value.as_object();
    double default_dog_speed = 1.0;
    if (const json::value* value = root.if_contains(DFLT)) {
      default_dog_speed = json::value_to<double>(*value);
    }
    model::Game game;
    for (const json::value& value : root.at(MAPS).as_array()) {
      game.AddMap(ParseMap(value.as_object(), default_dog_speed));
    }
    return game;
  }

}  // namespace json_loader
#include <cmath>
#include <limits>
#include <string>
#include <cmath>
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

    std::size_t ReadBagCapacity(const json::object& object,
                                std::string_view key, std::size_t fallback) {
      const auto* value = object.if_contains(key);
      if (!value) { return fallback; }
      std::uint64_t capacity;
      if (value->is_int64() && value->as_int64() >= 0) {
        capacity = static_cast<std::uint64_t>(value->as_int64());
      } else if (value->is_uint64()) {
        capacity = value->as_uint64();
      } else {
        throw std::invalid_argument("Bag capacity must be a nonnegative integer");
      }
      if (capacity > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("Bag capacity is too large");
      }
      return static_cast<std::size_t>(capacity);
    }

    MAP ParseMap(const json::object& map_json, double default_dog_speed,
                 std::size_t default_bag_capacity, extra_data::ExtraData& extra) {
      const json::value* configured_speed = map_json.if_contains(SPEEDOG);
      const double dog_speed = configured_speed
        ? json::value_to<double>(*configured_speed)
        : default_dog_speed;
      if (!std::isfinite(dog_speed) || dog_speed <= 0.0) {
        throw std::invalid_argument("Dog speed must be finite and positive");
      }
      MAP map{ MAP::Id{GetString(map_json, ID)}, GetString(map_json, NAME), dog_speed};
      map.SetBagCapacity(ReadBagCapacity(map_json, "bagCapacity", default_bag_capacity));
      const auto& loot_types = map_json.at("lootTypes").as_array();
      if (loot_types.empty() || loot_types.size() > std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument("Invalid lootTypes array size");
      }
      for (const auto& type : loot_types) {
        if (!type.is_object()) {
          throw std::invalid_argument("lootTypes entries must be objects");
        }
      }
      map.SetLootTypesCount(static_cast<unsigned>(loot_types.size()));
      for (std::size_t i = 0; i < loot_types.size(); ++i) {
        const auto& value = loot_types[i].as_object().at("value");
        std::uint64_t points;
        if (value.is_int64() && value.as_int64() >= 0) {
          points = static_cast<std::uint64_t>(value.as_int64());
        } else if (value.is_uint64()) {
          points = value.as_uint64();
        } else {
          throw std::invalid_argument("Loot value must be a nonnegative integer");
        }
        map.SetLootValue(static_cast<unsigned>(i), points);
      }
      extra.SetLootTypes(*map.GetId(), loot_types);
      for (const json::value& value : map_json.at(ROAD).as_array()) {
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

  model::Game LoadGame(const std::filesystem::path& json_path, extra_data::ExtraData& extra) {
    std::ifstream input{json_path};
    if (!input) {
      throw std::runtime_error("Cannot open config file: " + json_path.string());
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const json::value root_value = json::parse(buffer.str());
    const json::object& root = root_value.as_object();
    double default_dog_speed = 1.0;
    if (const json::value* value = root.if_contains(DFLT)) {
      default_dog_speed = json::value_to<double>(*value);
    }
    const auto default_bag_capacity = ReadBagCapacity(root, "defaultBagCapacity", 3);
    model::Game game;
    const auto& generator = root.at("lootGeneratorConfig").as_object();
    const double period = json::value_to<double>(generator.at("period"));
    const double probability = json::value_to<double>(generator.at("probability"));
    const long double milliseconds = static_cast<long double>(period) * 1000.0L;
    if (!std::isfinite(period) || milliseconds < 1.0L
        || milliseconds >= static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::invalid_argument("Loot period must fit positive milliseconds");
    }
    game.SetLootGeneratorConfig({std::chrono::milliseconds{
        static_cast<std::int64_t>(std::round(milliseconds))}, probability});
    extra_data::ExtraData loaded_extra;
    for (const json::value& value : root.at(MAPS).as_array()) {
      game.AddMap(ParseMap(value.as_object(), default_dog_speed, default_bag_capacity, loaded_extra));
    }
    extra = std::move(loaded_extra);
    return game;
  }

}  // namespace json_loader

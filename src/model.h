#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

#include "tagged.h"

namespace model {

  struct LootGeneratorConfig {
    std::chrono::milliseconds period{5000};
    double probability = 0.5;
  };

  using Dimension = int;
  using Coord = Dimension;

  struct Point {
    Coord x, y;
  };

  struct Size {
    Dimension width, height;
  };

  struct Rectangle {
    Point position;
    Size size;
  };

  struct Offset {
    Dimension dx, dy;
  };

  class Road {
    struct HorizontalTag {
      explicit HorizontalTag() = default;
    };

    struct VerticalTag {
      explicit VerticalTag() = default;
    };

    Point start_;
    Point end_;

  public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
      : start_{start}
      , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
      : start_{start}
      , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
      return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
      return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
      return start_;
    }

    Point GetEnd() const noexcept {
      return end_;
    }
  };

  class Building {
    Rectangle bounds_;

  public:
    explicit Building(Rectangle bounds) noexcept
      : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
      return bounds_;
    }
  };

  class Office {
  public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
      : id_{std::move(id)}
      , position_{position}
      , offset_{offset} {
    }

    const Id& GetId() const noexcept {
      return id_;
    }

    Point GetPosition() const noexcept {
      return position_;
    }

    Offset GetOffset() const noexcept {
      return offset_;
    }

  private:
    Id id_;
    Point position_;
    Offset offset_;
  };


  class Map {
  public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id, std::string name, double dog_speed = 1.0) noexcept
      : id_(std::move(id))
      , name_(std::move(name))
      , dog_speed_(dog_speed) {
    }

    const Id& GetId() const noexcept {
      return id_;
    }

    const std::string& GetName() const noexcept {
      return name_;
    }

    const Buildings& GetBuildings() const noexcept {
      return buildings_;
    }

    const Roads& GetRoads() const noexcept {
      return roads_;
    }

    const Offices& GetOffices() const noexcept {
      return offices_;
    }

    void AddRoad(const Road& road) {
      roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
      buildings_.emplace_back(building);
    }

    double GetDogSpeed() const noexcept {
      return dog_speed_;
    }

    void SetDogSpeed(double speed) noexcept {
      dog_speed_ = speed;
    }

    void SetLootTypesCount(unsigned count) {
      if (count == 0) {
        throw std::invalid_argument("lootTypes must not be empty");
      }
      loot_values_.resize(count, 0);
      loot_types_count_ = count;
    }

    unsigned GetLootTypesCount() const noexcept {
      return loot_types_count_;
    }

    void SetBagCapacity(std::size_t capacity) noexcept {
      bag_capacity_ = capacity;
    }

    std::size_t GetBagCapacity() const noexcept {
      return bag_capacity_;
    }

    void SetLootValue(unsigned type, std::uint64_t value) {
      loot_values_.at(type) = value;
    }

    std::uint64_t GetLootValue(unsigned type) const {
      return loot_values_.at(type);
    }


    void AddOffice(Office office);

  private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    Roads roads_;
    Buildings buildings_;
    double dog_speed_{ 1.0};
    unsigned loot_types_count_ = 0;
    std::vector<std::uint64_t> loot_values_;
    std::size_t bag_capacity_ = 3;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;

  };

  class Game {
    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;
    LootGeneratorConfig loot_generator_config_;
    std::vector<Map> maps_;
    MapIdToIndex map_id_to_index_;

  public:
      using Maps = std::vector<Map>;

      void SetLootGeneratorConfig(LootGeneratorConfig config) {
        if (config.period.count() <= 0 || !std::isfinite(config.probability)
            || config.probability < 0.0 || config.probability > 1.0) {
          throw std::invalid_argument("Invalid loot generator configuration");
        }
        loot_generator_config_ = config;
      }

      const LootGeneratorConfig& GetLootGeneratorConfig() const noexcept {
        return loot_generator_config_;
      }

      void AddMap(Map map);

      const Maps& GetMaps() const noexcept {
          return maps_;
      }

      const Map* FindMap(const Map::Id& id) const noexcept {
        auto it{ map_id_to_index_.find(id)};
        if( it != map_id_to_index_.end()){
          return &maps_.at(it->second);
        }
        return nullptr;
      }
  };
}  // namespace model

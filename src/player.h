#pragma once

#include <chrono>
#include <limits>
#include <array>
#include <chrono>
#include <stdexcept>
#include <deque>
#include <random>
#include <string>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>
#include <algorithm>
#include <unordered_map>

#include <vector>
#include <cmath>

#include "model.h"
#include "collision_detector.h"
#include "loot_generator.h"
#include "tagged.h"

namespace model {

  namespace detail {
    struct DogTag {};
    struct PlayerTag {};
    struct TokenTag {};
  } // namespace detail

  using DogId = util::Tagged<std::uint64_t, detail::DogTag>;
  using PlayerId = util::Tagged<std::uint64_t, detail::PlayerTag>;
  using Token = util::Tagged<std::string, detail::TokenTag>;

  struct Position {
    double x = 0.0;
    double y = 0.0;
  };

  struct LostObject {
    std::uint64_t id;
    unsigned type;
    Position position;
  };

  struct BagItem {
    std::uint64_t id;
    unsigned type;
  };

  struct Speed {
    double x = 0.0;
    double y = 0.0;
  };

  enum class Direction { NORTH, SOUTH, WEST, EAST, };

  class Dog {
    DogId id_;
    std::string name_;
    Position position_;
    Speed speed_{0.0, 0.0};
    Direction direction_ = Direction::NORTH;
    std::vector<BagItem> bag_;
    std::uint64_t score_ = 0;

  public:
    Dog(DogId id, std::string name, Position position) noexcept
      : id_(id)
      , name_(std::move(name))
      , position_(position) {
    }

    DogId GetId() const noexcept {
      return id_;
    }

    const std::string& GetName() const noexcept {
      return name_;
    }

    Position GetPosition() const noexcept {
      return position_;
    }

    Speed GetSpeed() const noexcept {
      return speed_;
    }

    Direction GetDirection() const noexcept {
      return direction_;
    }

    void SetMove(Direction direction, double dog_speed) noexcept {
      direction_ = direction;
      switch (direction) {
        case Direction::NORTH:
          speed_ = {0.0, -dog_speed};
          break;
        case Direction::SOUTH:
          speed_ = {0.0, dog_speed};
          break;
        case Direction::WEST:
          speed_ = {-dog_speed, 0.0};
          break;
        case Direction::EAST:
          speed_ = {dog_speed, 0.0};
          break;
      }
    }

    const std::vector<BagItem>& GetBag() const noexcept {
      return bag_;
    }

    bool TryCollect(const LostObject& object, std::size_t capacity) {
      if (bag_.size() >= capacity) {
        return false;
      }
      bag_.push_back({object.id, object.type});
      return true;
    }

    std::uint64_t GetScore() const noexcept { return score_; }

    void ReturnLoot(const Map& map) {
      std::uint64_t total = score_;
      for (const auto& item : bag_) {
        const auto value = map.GetLootValue(item.type);
        if (value > std::numeric_limits<std::uint64_t>::max() - total) {
          throw std::overflow_error("Player score overflow");
        }
        total += value;
      }
      score_ = total;
      bag_.clear();
    }

    void Stop() noexcept {
      speed_ = {0.0, 0.0};
    }

    void SetPosition(Position position) noexcept {
      position_ = position;
    }
  };

  class GameSession {
    Position GenerateRandomPositionOnRoad() {
      const auto& roads = map_->GetRoads();
      // По условиям игровая карта для входа должна иметь хотя бы одну дорогу.
      // Защита нужна, чтобы не допустить неопределённого поведения.
      //if (roads.empty()) {
      //  return {};
      //}
      if (roads.empty()) {
        throw std::logic_error("Cannot add dog to a map without roads");
      }
      std::uniform_int_distribution<size_t> road_distribution( 0, roads.size() - 1);
      const Road& road = roads[road_distribution(random_generator_)];
      const Point start = road.GetStart();
      const Point end = road.GetEnd();
      if (road.IsHorizontal()) {
        const int min_x = std::min(start.x, end.x);
        const int max_x = std::max(start.x, end.x);
        std::uniform_real_distribution<double> x_distribution(
          static_cast<double>(min_x),
          static_cast<double>(max_x));
        return {
          x_distribution(random_generator_),
          static_cast<double>(start.y),
        };
      }
      const int min_y = std::min(start.y, end.y);
      const int max_y = std::max(start.y, end.y);
      std::uniform_real_distribution<double> y_distribution( static_cast<double>(min_y), static_cast<double>(max_y));
      return { static_cast<double>(start.x), y_distribution(random_generator_), };
    }

    static constexpr double kHalfRoadWidth = 0.4;
    struct Interval { double from; double to; };

    std::vector<Interval> FindAvailableIntervals(Position pos, bool horizontal) const {
      std::vector<Interval> intervals;
      for (const auto& road : map_->GetRoads()) {
        const auto a = road.GetStart();
        const auto b = road.GetEnd();
        const double min_x = std::min(a.x, b.x) - kHalfRoadWidth;
        const double max_x = std::max(a.x, b.x) + kHalfRoadWidth;
        const double min_y = std::min(a.y, b.y) - kHalfRoadWidth;
        const double max_y = std::max(a.y, b.y) + kHalfRoadWidth;
        if (horizontal && pos.y >= min_y && pos.y <= max_y) {
          intervals.push_back({min_x, max_x});
        } else if (!horizontal && pos.x >= min_x && pos.x <= max_x) {
          intervals.push_back({min_y, max_y});
        }
      }
      std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
        return a.from < b.from;
      });
      std::vector<Interval> merged;
      for (const auto& interval : intervals) {
        if (merged.empty() || interval.from > merged.back().to) {
          merged.push_back(interval);
        } else {
          merged.back().to = std::max(merged.back().to, interval.to);
        }
      }
      return merged;
    }

    void MoveDog(Dog& dog, double seconds) const {
      const auto speed = dog.GetSpeed();
      if (speed.x == 0.0 && speed.y == 0.0) { return; }
      const bool horizontal = speed.x != 0.0;
      auto pos = dog.GetPosition();
      const double current = horizontal ? pos.x : pos.y;
      const double desired = current + (horizontal ? speed.x : speed.y) * seconds;
      for (const auto& interval : FindAvailableIntervals(pos, horizontal)) {
        if (current < interval.from || current > interval.to) { continue; }
        const double actual = std::clamp(desired, interval.from, interval.to);
        if (horizontal) { pos.x = actual; } else { pos.y = actual; }
        dog.SetPosition(pos);
        if (actual != desired) { dog.Stop(); }
        return;
      }
      dog.Stop();
    }

    class CollisionProvider final : public collision_detector::ItemGathererProvider {
    public:
      std::vector<collision_detector::Item> items;
      std::vector<collision_detector::Gatherer> gatherers;

      std::size_t ItemsCount() const override { return items.size(); }
      collision_detector::Item GetItem(std::size_t index) const override {
        return items.at(index);
      }
      std::size_t GatherersCount() const override { return gatherers.size(); }
      collision_detector::Gatherer GetGatherer(std::size_t index) const override {
        return gatherers.at(index);
      }
    };

    void CollectAndReturnLoot(const CollisionProvider& provider) {
      const auto events = collision_detector::FindGatherEvents(provider);
      const std::size_t loot_count = lost_objects_.size();
      std::vector<bool> collected(loot_count, false);
      for (const auto& event : events) {
        Dog& dog = dogs_.at(event.gatherer_id);
        if (event.item_id >= loot_count) {
          dog.ReturnLoot(*map_);
        } else if (!collected[event.item_id]
                   && dog.TryCollect(lost_objects_[event.item_id], map_->GetBagCapacity())) {
          collected[event.item_id] = true;
        }
      }
      // Preserve indices until every event has been processed.
      std::size_t write = 0;
      for (std::size_t read = 0; read < loot_count; ++read) {
        if (!collected[read]) {
          if (write != read) {
            lost_objects_[write] = std::move(lost_objects_[read]);
          }
          ++write;
        }
      }
      lost_objects_.resize(write);
    }

    const Map* map_;
    bool randomize_spawn_points_ = false;
    std::uint64_t next_dog_id_ = 0;
    std::mt19937 random_generator_;
    // deque сохраняет адреса объектов Dog при добавлении новых собак.
    std::deque<Dog> dogs_;
    loot_gen::LootGenerator loot_generator_;
    std::uint64_t next_loot_id_ = 0;
    std::vector<LostObject> lost_objects_;

    void GenerateLoot(std::chrono::milliseconds delta) {
      if (dogs_.size() > std::numeric_limits<unsigned>::max()
          || lost_objects_.size() > std::numeric_limits<unsigned>::max()) {
        throw std::overflow_error("Too many session objects");
      }
      const unsigned count = loot_generator_.Generate(
          delta, static_cast<unsigned>(lost_objects_.size()),
          static_cast<unsigned>(dogs_.size()));
      if (count == 0) {
        return;
      }
      std::uniform_int_distribution<unsigned> type_distribution(
          0, map_->GetLootTypesCount() - 1);
      for (unsigned i = 0; i < count; ++i) {
        const unsigned type = type_distribution(random_generator_);
        const Position position = GenerateRandomPositionOnRoad();
        AddLostObject(type, position);
      }
    }

  public:
    GameSession(const Map* map, bool randomize_spawn_points,
                LootGeneratorConfig config = {},
                loot_gen::LootGenerator::RandomGenerator loot_random = [] { return 1.0; },
                std::uint32_t seed = std::random_device{}())
      : map_(map)
      , randomize_spawn_points_(randomize_spawn_points)
      , random_generator_(seed)
      , loot_generator_(config.period, config.probability, std::move(loot_random)) {
      if (!map_ || map_->GetLootTypesCount() == 0 || map_->GetRoads().empty()) {
        throw std::invalid_argument("Session needs a map with roads and loot types");
      }
      if (config.period.count() <= 0 || !std::isfinite(config.probability)
          || config.probability < 0.0 || config.probability > 1.0) {
        throw std::invalid_argument("Invalid loot generator configuration");
      }
    }

    const Map& GetMap() const noexcept {
      return *map_;
    }

    void Tick(std::chrono::milliseconds delta) {
      if (delta.count() < 0) {
        throw std::invalid_argument("Negative tick interval");
      }
      if (delta.count() == 0) {
        return;
      }
      const double delta_seconds = std::chrono::duration<double>(delta).count();
      CollisionProvider provider;
      provider.items.reserve(lost_objects_.size() + map_->GetOffices().size());
      for (const auto& object : lost_objects_) {
        provider.items.push_back({{object.position.x, object.position.y}, 0.0});
      }
      for (const auto& office : map_->GetOffices()) {
        const auto position = office.GetPosition();
        provider.items.push_back({{static_cast<double>(position.x),
                                   static_cast<double>(position.y)}, 0.25});
      }
      provider.gatherers.reserve(dogs_.size());
      for (Dog& dog : dogs_) {
        const auto start = dog.GetPosition();
        MoveDog(dog, delta_seconds);
        const auto end = dog.GetPosition();
        provider.gatherers.push_back({{start.x, start.y}, {end.x, end.y}, 0.3});
      }
      CollectAndReturnLoot(provider);
      // Newly generated objects participate starting with the next tick.
      GenerateLoot(delta);
    }

    const std::vector<LostObject>& GetLostObjects() const noexcept {
      return lost_objects_;
    }

    LostObject& AddLostObject(unsigned type, Position position) {
      if (type >= map_->GetLootTypesCount()) {
        throw std::invalid_argument("Invalid loot type");
      }
      if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        throw std::invalid_argument("Invalid loot position");
      }
      lost_objects_.push_back({next_loot_id_, type, position});
      ++next_loot_id_;
      return lost_objects_.back();
    }

    Dog& AddDog(std::string name) {
      Position position;
      if (randomize_spawn_points_) {
        position = GenerateRandomPositionOnRoad();
      } else {
        const auto start = map_->GetRoads().front().GetStart();
        position = {static_cast<double>(start.x), static_cast<double>(start.y)};
      }
      const DogId id{next_dog_id_++};
      return dogs_.emplace_back(id, std::move(name), position);
    }

    const std::deque<Dog>& GetDogs() const noexcept {
      return dogs_;
    }
  };

  class Player {
    PlayerId id_;
    GameSession* session_;
    Dog* dog_;

  public:
    Player(PlayerId id, GameSession& session, Dog& dog) noexcept
      : id_(id)
      , session_(&session)
      , dog_(&dog) {
    }

    PlayerId GetId() const noexcept {
      return id_;
    }

    const GameSession& GetSession() const noexcept {
      return *session_;
    }

    Dog& GetDog() noexcept {
      return *dog_;
    }

    const Dog& GetDog() const noexcept {
      return *dog_;
    }
  };

  class Players {
    std::uint64_t next_player_id_ = 0;
    std::unordered_map<PlayerId, Player, util::TaggedHasher<PlayerId>> players_;

  public:
    Player& Add(Dog& dog, GameSession& session) {
      const PlayerId id{next_player_id_++};
      auto [it, inserted] = players_.emplace(
        id,
        Player{id, session, dog});
      return it->second;
    }

    Player* FindById(PlayerId id) noexcept {
      if (auto it = players_.find(id); it != players_.end()) {
        return &it->second;
      }
      return nullptr;
    }

    auto begin() const noexcept {
      return players_.begin();
    }

    auto end() const noexcept {
      return players_.end();
    }
  };

  class PlayerTokens {
    std::string GenerateToken() {
      const std::array values{
        generator_(),
        generator_()
      };
      std::ostringstream output;
      output << std::hex << std::setfill('0')
          << std::setw(16) << values[0]
          << std::setw(16) << values[1];
      return output.str();
    }

    std::mt19937_64 generator_;
    std::unordered_map<Token, Player*, util::TaggedHasher<Token>> token_to_player_;

  public:
    PlayerTokens()
      : generator_(std::random_device{}()) {
    }

    Token AddPlayer(Player& player) {
      Token token{std::string{}};
      do {
        token = Token{GenerateToken()};
      } while (token_to_player_.contains(token));
      token_to_player_.emplace(token, &player);
      return token;
    }

    Player* FindPlayerByToken(const Token& token) const noexcept {
      if (auto it = token_to_player_.find(token);
        it != token_to_player_.end()) {
        return it->second;
      }
      return nullptr;
    }
  };
} // namespace model

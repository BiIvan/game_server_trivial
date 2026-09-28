#pragma once

#include <array>
#include <deque>
#include <random>
#include <string>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "model.h"
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
  
  struct Speed {
    double x = 0.0;
    double y = 0.0;
  };
  
  enum class Direction {
    NORTH,
    SOUTH,
    WEST,
    EAST,
  };
  
  class Dog {
    DogId id_;
    std::string name_;
    Position position_;
    Speed speed_{0.0, 0.0};
    Direction direction_ = Direction::NORTH;
    
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

    void Stop() noexcept {
      speed_ = {0.0, 0.0};
    }    
  };
  
  class GameSession {
    Position GenerateRandomPositionOnRoad() {
      const auto& roads = map_->GetRoads();
      // По условиям игровая карта для входа должна иметь хотя бы одну дорогу.
      // Защита нужна, чтобы не допустить неопределённого поведения.
      if (roads.empty()) {
        return {};
      }
      std::uniform_int_distribution<size_t> road_distribution(
        0, roads.size() - 1);
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
      std::uniform_real_distribution<double> y_distribution(
        static_cast<double>(min_y),
        static_cast<double>(max_y));
      return {
        static_cast<double>(start.x),
        y_distribution(random_generator_),
      };
    }
    
    const Map* map_;
    std::uint64_t next_dog_id_ = 0;
    std::mt19937 random_generator_;
    // deque сохраняет адреса объектов Dog при добавлении новых собак.
    std::deque<Dog> dogs_;
    
  public:
    explicit GameSession(const Map* map)
      : map_(map)
      , random_generator_(std::random_device{}()) {
    }
    
    const Map& GetMap() const noexcept {
      return *map_;
    }
    
    Dog& AddDog(std::string name) {
      const DogId id{next_dog_id_++};
      return dogs_.emplace_back(
        id,
        std::move(name),
        GenerateRandomPositionOnRoad());
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
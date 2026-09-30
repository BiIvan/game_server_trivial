#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <utility>
#include <stdexcept>
#include <unordered_map>

#include "model.h"
#include "player.h"
#include "literals.h"

using GS = model::GameSession;

namespace app {

  class Application {
    GS& GetOrCreateSession(const model::Map& map) {
      const std::string map_id { *map.GetId()};
      if (auto it = sessions_.find(map_id);
        it != sessions_.end()) {
        return *it->second;
      }
      auto session = std::make_unique<GS>( &map, randomize_spawn_points_);
      GS* result = session.get();
      sessions_.emplace(map_id, std::move(session));
      return *result;
    }

    model::Game& game_;
    model::Players players_;
    model::PlayerTokens tokens_;
    bool randomize_spawn_points_ = false;
    std::unordered_map<std::string,std::unique_ptr<GS>> sessions_;
    
  public:
    explicit Application( model::Game& game, bool randomize_spawn_points)
      : game_(game)
      , randomize_spawn_points_(randomize_spawn_points) {
    }
    
    void Tick(std::int64_t delta_ms) {
      const double delta_seconds = static_cast<double>(delta_ms) / 1000.0;
      for (auto& [map_id, session] : sessions_) {
        session->Tick(delta_seconds);
      }
    }

    struct JoinResult { 
      model::PlayerId player_id;
      model::Token token;
    };

    JoinResult JoinGame( const model::Map::Id& map_id, std::string user_name) {
      const model::Map* map{ game_.FindMap(map_id)};
      if (map == nullptr) {
        throw std::out_of_range(std::string{NOMAPstr});
      }
      GS& session = GetOrCreateSession(*map);
      model::Dog& dog = session.AddDog(std::move(user_name));
      model::Player& player = players_.Add(dog, session);
      model::Token token = tokens_.AddPlayer(player);
      return {player.GetId(), std::move(token)};
    }

    model::Player* FindPlayerByToken( const model::Token& token) const noexcept {
      return tokens_.FindPlayerByToken(token);
    }

    const model::Players& GetPlayers() const noexcept {
      return players_;
    }
  };

}  // namespace app
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "model.h"
#include "player.h"

namespace app {

class Application {
public:
    explicit Application(model::Game& game)
        : game_(game) {
    }

    struct JoinResult {
        model::PlayerId player_id;
        model::Token token;
    };

    JoinResult JoinGame(
        const model::Map::Id& map_id,
        std::string user_name) {

        const model::Map* map = game_.FindMap(map_id);

        if (map == nullptr) {
            throw std::out_of_range("Map not found");
        }

        model::GameSession& session = GetOrCreateSession(*map);
        model::Dog& dog = session.AddDog(std::move(user_name));
        model::Player& player = players_.Add(dog, session);
        model::Token token = tokens_.AddPlayer(player);

        return {player.GetId(), std::move(token)};
    }

    model::Player* FindPlayerByToken(
        const model::Token& token) const noexcept {

        return tokens_.FindPlayerByToken(token);
    }

    const model::Players& GetPlayers() const noexcept {
        return players_;
    }

private:
    model::GameSession& GetOrCreateSession(const model::Map& map) {
        const std::string map_id = *map.GetId();

        if (auto it = sessions_.find(map_id); it != sessions_.end()) {
            return *it->second;
        }

        auto session = std::make_unique<model::GameSession>(&map);
        model::GameSession* result = session.get();

        sessions_.emplace(map_id, std::move(session));
        return *result;
    }

    model::Game& game_;
    model::Players players_;
    model::PlayerTokens tokens_;

    std::unordered_map<
        std::string,
        std::unique_ptr<model::GameSession>> sessions_;
};

}  // namespace app
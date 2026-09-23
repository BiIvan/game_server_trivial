#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "model.h"
#include "tagged.h"

namespace model {

namespace detail {

struct DogTag {};
struct PlayerTag {};
struct TokenTag {};

}  // namespace detail

using DogId = util::Tagged<std::uint64_t, detail::DogTag>;
using PlayerId = util::Tagged<std::uint64_t, detail::PlayerTag>;
using Token = util::Tagged<std::string, detail::TokenTag>;

class Dog {
public:
    Dog(DogId id, std::string name)
        : id_(id)
        , name_(std::move(name)) {
    }

    DogId GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

private:
    DogId id_;
    std::string name_;
};

class GameSession {
public:
    explicit GameSession(const Map* map)
        : map_(map) {
    }

    const Map& GetMap() const noexcept {
        return *map_;
    }

    Dog& AddDog(std::string name) {
        const DogId id{next_dog_id_++};
        return dogs_.emplace_back(id, std::move(name));
    }

    const std::deque<Dog>& GetDogs() const noexcept {
        return dogs_;
    }

private:
    const Map* map_;
    std::uint64_t next_dog_id_ = 0;

    // deque нужен, чтобы адреса Dog не менялись при добавлении новых собак.
    // Player хранит Dog*, поэтому vector здесь использовать опасно.
    std::deque<Dog> dogs_;
};

class Player {
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

    const Dog& GetDog() const noexcept {
        return *dog_;
    }

private:
    PlayerId id_;
    GameSession* session_;
    Dog* dog_;
};

class Players {
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

private:
    std::uint64_t next_player_id_ = 0;

    // Указатели на элементы unordered_map не инвалидируются при rehash.
    // Это важно, поскольку PlayerTokens хранит Player*.
    std::unordered_map<
        PlayerId,
        Player,
        util::TaggedHasher<PlayerId>> players_;
};

class PlayerTokens {
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

private:
    std::string GenerateToken() {
        const std::array<std::uint64_t, 2> values{
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

    std::unordered_map<
        Token,
        Player*,
        util::TaggedHasher<Token>> token_to_player_;
};

}  // namespace model
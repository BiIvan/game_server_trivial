#include <stdexcept>

#include "model.h"
#include "literals.h"

namespace model {
  using namespace std::literals;

  void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
      throw std::invalid_argument(std::string{DUPLICATE});
    }
    const size_t index = offices_.size();
    Office& o = offices_.emplace_back(std::move(office));
    try {
      warehouse_id_to_index_.emplace(o.GetId(), index);
    } catch (...) {
      offices_.pop_back();
      throw;
    }
 }

  void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
      throw std::invalid_argument("Map with id "s + *map.GetId() + std::string{EXIST});
    } else {
      try {
        maps_.emplace_back(std::move(map));
      } catch (...) {
        map_id_to_index_.erase(it);
        throw;
      }
    }
  }
}  // namespace model

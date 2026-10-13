#pragma once

#include <string>
#include <unordered_map>
#include <boost/json.hpp>

namespace extra_data {

class ExtraData {
public:
    void SetLootTypes(std::string map_id, boost::json::array types) {
        loot_types_.insert_or_assign(std::move(map_id), std::move(types));
    }

    const boost::json::array& GetLootTypes(const std::string& map_id) const {
        return loot_types_.at(map_id);
    }

private:
    std::unordered_map<std::string, boost::json::array> loot_types_;
};

}  // namespace extra_data

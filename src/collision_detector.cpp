#include "collision_detector.h"
#include <cassert>

namespace collision_detector {

CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
    // Проверим, что перемещение ненулевое.
    // Тут приходится использовать строгое равенство, а не приближённое,
    // пскольку при сборе заказов придётся учитывать перемещение даже на небольшое
    // расстояние.
    assert(b.x != a.x || b.y != a.y);
    const double u_x = c.x - a.x;
    const double u_y = c.y - a.y;
    const double v_x = b.x - a.x;
    const double v_y = b.y - a.y;
    const double u_dot_v = u_x * v_x + u_y * v_y;

    const double v_len2 = v_x * v_x + v_y * v_y;
    const double proj_ratio = u_dot_v / v_len2;
    const double distance_x = u_x - proj_ratio * v_x;
    const double distance_y = u_y - proj_ratio * v_y;
    const double sq_distance = distance_x * distance_x + distance_y * distance_y;

    return CollectionResult(sq_distance, proj_ratio);
}

  std::vector<GatheringEvent> FindGatherEvents(
    const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> events;
    const size_t items_count = provider.ItemsCount();
    const size_t gatherers_count = provider.GatherersCount();
    for (size_t gatherer_id = 0;
       gatherer_id < gatherers_count;
       ++gatherer_id) {
      const Gatherer gatherer = provider.GetGatherer(gatherer_id);
      // TryCollectPoint требует ненулевого перемещения.
      if (gatherer.start_pos.x == gatherer.end_pos.x &&
        gatherer.start_pos.y == gatherer.end_pos.y) {
          continue;
      }
      for (size_t item_id = 0; item_id < items_count; ++item_id) {
        const Item item = provider.GetItem(item_id);
        const CollectionResult result = TryCollectPoint(
          gatherer.start_pos,
          gatherer.end_pos,
          item.position);
        const double collect_radius = gatherer.width + item.width;
        if (result.IsCollected(collect_radius)) {
          events.push_back(GatheringEvent{
            .item_id = item_id,
            .gatherer_id = gatherer_id,
            .sq_distance = result.sq_distance,
            .time = result.proj_ratio,
          });
        }
      }
    }
    std::sort(
      events.begin(),
      events.end(),
      [](const GatheringEvent& lhs, const GatheringEvent& rhs) {
        return lhs.time < rhs.time;
      });
    return events;
  }
}  // namespace collision_detector
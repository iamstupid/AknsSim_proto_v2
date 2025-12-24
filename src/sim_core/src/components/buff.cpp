#include "sim_core/components/buff.hpp"

#include <utility>

namespace arksim {

void Buff::add_target(Entity target) {
  targets.push_back(target);
}

void Buff::step(World& world, Entity self, Tick tick_rate) {
  if (life_remain < 0) {
    return;
  }

  const std::int64_t rate = static_cast<std::int64_t>(tick_rate);
  if (life_remain >= 0 && life_remain < rate) {
    destruct(world, self);
    return;
  }

  life_remain -= rate;
}

void Buff::destruct(World& world, Entity self) {
  OnDestruct(world, self, *this);
  world.destroy(self);
}

Entity make_buff(World& world, Entity target, std::int64_t life_remain, Entity giver) {
  Entity buff_entity = world.create();

  Buff buff;
  buff.giver = giver;
  buff.life_remain = life_remain;
  buff.add_target(target);
  world.add<Buff>(buff_entity, buff);

  return buff_entity;
}

void link_buff_damage_processor(World& world,
                                Entity buff_entity,
                                Entity target,
                                DefStats& stats,
                                int priority,
                                DamageProcessor proc) {
  stats.dagr.add(buff_entity, priority, std::move(proc));
  auto& buff = world.get<Buff>(buff_entity);
  buff.OnDestruct.add(entity_key(target), priority, [target](World& world_ref, Entity self, Buff&) {
    if (auto* ds = world_ref.try_get<DefStats>(target)) {
      ds->dagr.erase(self);
    }
  });
}

} // namespace arksim

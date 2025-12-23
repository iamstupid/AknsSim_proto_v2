#include "sim_core/components/buff.hpp"

#include <utility>

namespace arksim {

void Buff::add_target(flecs::entity target) {
  targets.push_back(target);
}

void Buff::step(flecs::entity self, Tick tick_rate) {
  if (life_remain < 0) {
    return;
  }

  const std::int64_t rate = static_cast<std::int64_t>(tick_rate);
  if (life_remain >= 0 && life_remain < rate) {
    destruct(self);
    return;
  }

  life_remain -= rate;
}

void Buff::destruct(flecs::entity self) {
  OnDestruct(self, *this);
  self.destruct();
}

flecs::entity make_buff(flecs::entity target, std::int64_t life_remain, flecs::entity giver) {
  flecs::world world = target.world();
  flecs::entity buff_entity = world.entity();

  Buff buff;
  buff.giver = giver;
  buff.life_remain = life_remain;
  buff.add_target(target);
  buff_entity.set<Buff>(buff);

  return buff_entity;
}

void link_buff_damage_processor(flecs::entity buff_entity,
                                flecs::entity target,
                                DefStats& stats,
                                int priority,
                                DamageProcessor proc) {
  stats.dagr.add(buff_entity, priority, std::move(proc));
  auto& buff = buff_entity.get_mut<Buff>();
  buff.OnDestruct.add(target.id(), priority, [target](flecs::entity self, Buff&) {
    if (auto* ds = target.try_get_mut<DefStats>()) {
      ds->dagr.erase(self);
      target.modified<DefStats>();
    }
  });
  target.modified<DefStats>();
}

} // namespace arksim

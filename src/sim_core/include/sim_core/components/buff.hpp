#pragma once

#include <cstdint>
#include <vector>

#include <flecs.h>

#include "sim_core/components/damage.hpp"
#include "sim_core/types.hpp"

namespace arksim {

struct Buff {
  flecs::entity giver{};
  std::vector<flecs::entity> targets;
  std::int64_t life_remain = -1; // -1 means infinite
  TriggerProcessor<flecs::entity, Buff&> OnDestruct;

  void add_target(flecs::entity target);
  void step(flecs::entity self, Tick tick_rate);
  void destruct(flecs::entity self);
};

flecs::entity make_buff(flecs::entity target,
                        std::int64_t life_remain = -1,
                        flecs::entity giver = {});
void link_buff_damage_processor(flecs::entity buff_entity,
                                flecs::entity target,
                                DefStats& stats,
                                int priority,
                                DamageProcessor proc);

} // namespace arksim

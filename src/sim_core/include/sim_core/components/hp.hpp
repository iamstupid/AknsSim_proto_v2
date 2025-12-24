#pragma once

#include "sim_core/ecs.hpp"
#include "sim_core/types.hpp"

namespace arksim {

struct HP {
  BuffNum total_hp;
  double ratio = 1.0; // actual hp is ratio * total_hp; ratio <= 1 in most cases
  TriggerProcessor<World&, Entity, Entity, double> OnUnderflow;
  TriggerProcessor<World&, Entity, Entity, double> OnOverflow;
};

void do_damage(World& world, Entity object, double amount, Entity source);
void do_heal(World& world, Entity object, double amount, Entity source);

} // namespace arksim

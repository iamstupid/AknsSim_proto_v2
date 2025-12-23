#pragma once

#include <flecs.h>

#include "sim_core/types.hpp"

namespace arksim {

struct HP {
  BuffNum total_hp;
  double ratio = 1.0; // actual hp is ratio * total_hp; ratio <= 1 in most cases
  TriggerProcessor<flecs::entity, flecs::entity, double> OnUnderflow;
  TriggerProcessor<flecs::entity, flecs::entity, double> OnOverflow;
};

void do_damage(flecs::entity object, double amount, flecs::entity source);
void do_heal(flecs::entity object, double amount, flecs::entity source);

} // namespace arksim

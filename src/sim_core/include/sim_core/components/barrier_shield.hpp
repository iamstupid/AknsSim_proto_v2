#pragma once

#include <cstdint>

#include <flecs.h>

#include "sim_core/components/buff.hpp"
#include "sim_core/components/damage.hpp"

namespace arksim {

struct Barrier {
  flecs::entity self{};
  flecs::entity object{};
  double amount = 0.0;
  double amount_decrease_per_tick = 0.0;

  void bind(flecs::entity self_entity, flecs::entity object_entity);
  void UseBarrier(double discount);
  void step();
};

struct Shield {
  int hp = 0;
  int max_hp = 0;
  int delta_per_interval = 0; // k per interval
  Tick interval = 0;
  Tick interval_remain = 0;
  bool reset_timer_on_break = false;
  bool auto_destruct = true;
  flecs::entity self{};
  flecs::entity object{};
  TriggerProcessor<flecs::entity, Damage> OnShieldBlock;

  void bind(flecs::entity self_entity, flecs::entity object_entity);
  void UseShield(Damage dmg);
  void step(Tick tick_rate);

private:
  void on_break();
};

flecs::entity make_barrier(flecs::entity target,
                           Barrier barrier,
                           std::int64_t life_remain = -1,
                           flecs::entity giver = {},
                           int priority = 0,
                           Damage::TypeMask mask = Damage::ALL);
flecs::entity make_barrier(flecs::entity target,
                           double hp,
                           double hp_decay,
                           std::int64_t life_remain = -1,
                           flecs::entity giver = {},
                           int priority = 0,
                           Damage::TypeMask mask = Damage::ALL);
flecs::entity make_shield(flecs::entity target,
                          Shield shield,
                          std::int64_t life_remain = -1,
                          flecs::entity giver = {},
                          int priority = 0,
                          Damage::TypeMask mask = Damage::ALL);
flecs::entity make_shield(flecs::entity target,
                          int hp,
                          std::int64_t life_remain = -1,
                          flecs::entity giver = {},
                          int priority = 0,
                          Damage::TypeMask mask = Damage::ALL);

} // namespace arksim

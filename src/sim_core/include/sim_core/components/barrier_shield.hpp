#pragma once

#include <cstdint>

#include "sim_core/ecs.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/damage.hpp"

namespace arksim {

struct Barrier {
  Entity self{};
  Entity object{};
  double amount = 0.0;
  double amount_decrease_per_tick = 0.0;

  void bind(Entity self_entity, Entity object_entity);
  void UseBarrier(double discount);
  void step(World& world);
};

struct Shield {
  int hp = 0;
  int max_hp = 0;
  int delta_per_interval = 0; // k per interval
  Tick interval = 0;
  Tick interval_remain = 0;
  bool reset_timer_on_break = false;
  bool auto_destruct = true;
  Entity self{};
  Entity object{};
  TriggerProcessor<World&, Entity, Damage> OnShieldBlock;

  void bind(Entity self_entity, Entity object_entity);
  void UseShield(World& world, Damage dmg);
  void step(World& world, Tick tick_rate);

private:
  void on_break(World& world);
};

Entity make_barrier(World& world,
                    Entity target,
                           Barrier barrier,
                           std::int64_t life_remain = -1,
                           Entity giver = {},
                           int priority = 0,
                           Damage::TypeMask mask = Damage::ALL);
Entity make_barrier(World& world,
                    Entity target,
                           double hp,
                           double hp_decay,
                           std::int64_t life_remain = -1,
                           Entity giver = {},
                           int priority = 0,
                           Damage::TypeMask mask = Damage::ALL);
Entity make_shield(World& world,
                   Entity target,
                          Shield shield,
                          std::int64_t life_remain = -1,
                          Entity giver = {},
                          int priority = 0,
                          Damage::TypeMask mask = Damage::ALL);
Entity make_shield(World& world,
                   Entity target,
                          int hp,
                          std::int64_t life_remain = -1,
                          Entity giver = {},
                          int priority = 0,
                          Damage::TypeMask mask = Damage::ALL);

} // namespace arksim

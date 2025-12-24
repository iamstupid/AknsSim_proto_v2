#include "sim_core/components/barrier_shield.hpp"

#include <algorithm>

namespace arksim {

void Barrier::bind(Entity self_entity, Entity object_entity) {
  self = self_entity;
  object = object_entity;
}

void Barrier::UseBarrier(double discount) {
  amount -= discount;
  if (amount <= 0.0) {
    amount = 0.0;
  }
}

void Barrier::step(World& world) {
  if (!world.is_alive(self)) {
    return;
  }
  if (amount_decrease_per_tick > 0.0) {
    amount -= amount_decrease_per_tick;
    if (amount <= 0.0) {
      amount = 0.0;
    }
  }
  if (amount <= 0.0) {
    if (auto* buff = world.try_get<Buff>(self)) {
      buff->destruct(world, self);
    } else {
      world.destroy(self);
    }
  }
}

void Shield::bind(Entity self_entity, Entity object_entity) {
  self = self_entity;
  object = object_entity;
}

void Shield::UseShield(World& world, Damage dmg) {
  if (hp <= 0) {
    return;
  }
  --hp;
  OnShieldBlock(world, object, dmg);
}

void Shield::step(World& world, Tick tick_rate) {
  if (interval == 0 || delta_per_interval == 0 || max_hp <= 0) {
    if (hp <= 0 && auto_destruct) {
      on_break(world);
    }
    return;
  }

  if (interval_remain == 0) {
    interval_remain = interval;
  }

  if (interval_remain <= tick_rate) {
    const int next = std::clamp(hp + delta_per_interval, 0, max_hp);
    hp = next;
    interval_remain = interval;
  } else {
    interval_remain -= tick_rate;
  }

  if (hp <= 0 && auto_destruct) {
    on_break(world);
  }
}

void Shield::on_break(World& world) {
  if (reset_timer_on_break && interval > 0) {
    interval_remain = interval;
  }
  if (auto_destruct && world.is_alive(self)) {
    if (auto* buff = world.try_get<Buff>(self)) {
      buff->destruct(world, self);
    } else {
      world.destroy(self);
    }
  }
}

Entity make_barrier(World& world,
                    Entity target,
                           Barrier barrier,
                           std::int64_t life_remain,
                           Entity giver,
                           int priority,
                           Damage::TypeMask mask) {
  Entity buff_entity = make_buff(world, target, life_remain, giver);

  barrier.bind(buff_entity, target);
  world.add<Barrier>(buff_entity, barrier);

  if (auto* stats = world.try_get<DefStats>(target)) {
    link_buff_damage_processor(world, buff_entity, target, *stats, priority,
                               make_barrier_proc(buff_entity, mask));
  }

  return buff_entity;
}

Entity make_barrier(World& world,
                    Entity target,
                           double hp,
                           double hp_decay,
                           std::int64_t life_remain,
                           Entity giver,
                           int priority,
                           Damage::TypeMask mask) {
  Barrier barrier;
  barrier.amount = hp;
  barrier.amount_decrease_per_tick = hp_decay;
  return make_barrier(world, target, barrier, life_remain, giver, priority, mask);
}

Entity make_shield(World& world,
                   Entity target,
                          Shield shield,
                          std::int64_t life_remain,
                          Entity giver,
                          int priority,
                          Damage::TypeMask mask) {
  Entity buff_entity = make_buff(world, target, life_remain, giver);

  shield.bind(buff_entity, target);
  if (shield.max_hp <= 0) {
    shield.max_hp = shield.hp;
  }
  world.add<Shield>(buff_entity, shield);

  if (auto* stats = world.try_get<DefStats>(target)) {
    link_buff_damage_processor(world, buff_entity, target, *stats, priority,
                               make_shield_proc(buff_entity, mask));
  }

  return buff_entity;
}

Entity make_shield(World& world,
                   Entity target,
                          int hp,
                          std::int64_t life_remain,
                          Entity giver,
                          int priority,
                          Damage::TypeMask mask) {
  Shield shield;
  shield.hp = hp;
  shield.max_hp = hp;
  return make_shield(world, target, shield, life_remain, giver, priority, mask);
}

} // namespace arksim

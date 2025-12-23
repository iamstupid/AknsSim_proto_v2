#include "sim_core/components/barrier_shield.hpp"

#include <algorithm>

namespace arksim {

void Barrier::bind(flecs::entity self_entity, flecs::entity object_entity) {
  self = self_entity;
  object = object_entity;
}

void Barrier::UseBarrier(double discount) {
  amount -= discount;
  if (amount <= 0.0) {
    amount = 0.0;
  }
}

void Barrier::step() {
  if (!self.is_alive()) {
    return;
  }
  if (amount_decrease_per_tick > 0.0) {
    amount -= amount_decrease_per_tick;
    if (amount <= 0.0) {
      amount = 0.0;
    }
  }
  if (amount <= 0.0) {
    if (auto* buff = self.try_get_mut<Buff>()) {
      buff->destruct(self);
    } else {
      self.destruct();
    }
  }
}

void Shield::bind(flecs::entity self_entity, flecs::entity object_entity) {
  self = self_entity;
  object = object_entity;
}

void Shield::UseShield(Damage dmg) {
  if (hp <= 0) {
    return;
  }
  --hp;
  OnShieldBlock(object, dmg);
}

void Shield::step(Tick tick_rate) {
  if (interval == 0 || delta_per_interval == 0 || max_hp <= 0) {
    if (hp <= 0 && auto_destruct) {
      on_break();
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
    on_break();
  }
}

void Shield::on_break() {
  if (reset_timer_on_break && interval > 0) {
    interval_remain = interval;
  }
  if (auto_destruct && self.is_alive()) {
    if (auto* buff = self.try_get_mut<Buff>()) {
      buff->destruct(self);
    } else {
      self.destruct();
    }
  }
}

flecs::entity make_barrier(flecs::entity target,
                           Barrier barrier,
                           std::int64_t life_remain,
                           flecs::entity giver,
                           int priority,
                           Damage::TypeMask mask) {
  flecs::entity buff_entity = make_buff(target, life_remain, giver);

  barrier.bind(buff_entity, target);
  buff_entity.set<Barrier>(barrier);

  if (auto* stats = target.try_get_mut<DefStats>()) {
    link_buff_damage_processor(buff_entity, target, *stats, priority,
                               make_barrier_proc(buff_entity, mask));
  }

  return buff_entity;
}

flecs::entity make_barrier(flecs::entity target,
                           double hp,
                           double hp_decay,
                           std::int64_t life_remain,
                           flecs::entity giver,
                           int priority,
                           Damage::TypeMask mask) {
  Barrier barrier;
  barrier.amount = hp;
  barrier.amount_decrease_per_tick = hp_decay;
  return make_barrier(target, barrier, life_remain, giver, priority, mask);
}

flecs::entity make_shield(flecs::entity target,
                          Shield shield,
                          std::int64_t life_remain,
                          flecs::entity giver,
                          int priority,
                          Damage::TypeMask mask) {
  flecs::entity buff_entity = make_buff(target, life_remain, giver);

  shield.bind(buff_entity, target);
  if (shield.max_hp <= 0) {
    shield.max_hp = shield.hp;
  }
  buff_entity.set<Shield>(shield);

  if (auto* stats = target.try_get_mut<DefStats>()) {
    link_buff_damage_processor(buff_entity, target, *stats, priority,
                               make_shield_proc(buff_entity, mask));
  }

  return buff_entity;
}

flecs::entity make_shield(flecs::entity target,
                          int hp,
                          std::int64_t life_remain,
                          flecs::entity giver,
                          int priority,
                          Damage::TypeMask mask) {
  Shield shield;
  shield.hp = hp;
  shield.max_hp = hp;
  return make_shield(target, shield, life_remain, giver, priority, mask);
}

} // namespace arksim

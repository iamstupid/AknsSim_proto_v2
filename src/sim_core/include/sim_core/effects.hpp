#pragma once

#include <cstdint>

#include "sim_core/components/damage.hpp"
#include "sim_core/effect.hpp"
#include "sim_core/ecs.hpp"

namespace arksim {

enum class EffectType : std::uint8_t {
  None = 0,
  Damage = 1,
};

struct DamageEffectPayload {
  std::uint32_t src_gen = 0;
  std::uint32_t dst_idx = 0;
  std::uint32_t dst_gen = 0;
  std::uint32_t dmg_type = 0;
  double amount = 0.0;
};

static_assert(sizeof(DamageEffectPayload) == 24, "DamageEffectPayload must fit in Effect::param_buffer");

inline Effect make_damage_effect(Entity from, Entity to, Damage dmg) {
  Effect e;
  e.type = static_cast<std::uint32_t>(EffectType::Damage);
  e.src = from.entity_idx;

  DamageEffectPayload payload;
  payload.src_gen = from.gen;
  payload.dst_idx = to.entity_idx;
  payload.dst_gen = to.gen;
  payload.dmg_type = dmg.type;
  payload.amount = dmg.amount;
  e.param_buffer.store(payload);

  return e;
}

void register_core_effect_handlers(EffectHandler& handler);

} // namespace arksim


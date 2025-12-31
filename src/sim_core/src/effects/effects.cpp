#include "sim_core/effects/effects.hpp"

#include "sim_core/runtime/sim_state.hpp"
#include "sim_core/runtime/world_runtime.hpp"

namespace arksim {
namespace {

void handle_damage_effect(SimState* sim, const Effect& effect) {
  if (!sim) {
    return;
  }

  World& world = sim->world();
  const DamageEffectPayload& payload = effect.param_buffer.load<DamageEffectPayload>();

  Entity from = world.resolve_idx_gen(effect.src, payload.src_gen);
  if (from.entity_id == 0) {
    from = Entity{};
  }

  const Entity to = world.resolve_idx_gen(payload.dst_idx, payload.dst_gen);
  if (to.entity_id == 0) {
    return;
  }

  Damage dmg;
  dmg.amount = payload.amount;
  dmg.type = payload.dmg_type;
  make_hit(world, sim, from, to, dmg);
}

void handle_leak_effect(SimState* sim, const Effect& effect) {
  if (!sim) {
    return;
  }

  World& world = sim->world();
  const LeakEffectPayload& payload = effect.param_buffer.load<LeakEffectPayload>();

  Entity leaked = world.resolve_idx_gen(effect.src, payload.src_gen);
  if (leaked.entity_id == 0) {
    // Entity might have been destroyed already (e.g. if effects weren't drained before next frame).
    // Still pass a stable identifier to the event callback.
    leaked = Entity{payload.src_entity_id, effect.src, payload.src_gen};
  }

  sim->world_runtime().OnLeak(world, sim, leaked);
}

} // namespace

void register_core_effect_handlers(EffectHandler& handler) {
  handler.set(static_cast<std::uint8_t>(EffectType::Damage), &handle_damage_effect);
  handler.set(static_cast<std::uint8_t>(EffectType::Leak), &handle_leak_effect);
}

} // namespace arksim

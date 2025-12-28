#include "sim_core/effects.hpp"

#include "sim_core/sim_state.hpp"

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

} // namespace

void register_core_effect_handlers(EffectHandler& handler) {
  handler.set(static_cast<std::uint8_t>(EffectType::Damage), &handle_damage_effect);
}

} // namespace arksim


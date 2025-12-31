#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim_core/effects/effect.hpp"
#include "sim_core/ecs/ecs.hpp"
#include "sim_core/core/rng.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/scripting/world_api.hpp"

namespace arksim {

class ScriptVM;
struct SimContext;

class SimState {
public:
  explicit SimState(std::uint64_t seed = 0);

  Tick tick() const { return tick_; }
  Rng& rng() { return rng_; }
  const Rng& rng() const { return rng_; }
  World& world() { return world_; }
  const World& world() const { return world_; }
  WorldApi world_api() { return WorldApi{world_, this}; }

  void set_tick_rate(Tick tick_rate);
  void set_tick_rate(std::uint64_t numerator, std::uint64_t denominator);
  float get_tick_rate_f32() const;
  double get_tick_rate_f64() const;

  void attach_script(ScriptVM* script);
  ScriptVM* script_vm() { return script_; }
  const ScriptVM* script_vm() const { return script_; }
  EffectHandler& effect_handler() { return effect_handler_; }
  const EffectHandler& effect_handler() const { return effect_handler_; }

  void step();
  // Step one simulation frame using a deterministic core pipeline (movement -> spatial -> combat -> effects).
  // Returns number of effects processed in this frame (clamped by max_effects).
  std::size_t step_frame(SimContext& ctx, std::size_t max_effects = 100'000);
  void emit_effect(Effect effect);
  bool try_pop_effect(Effect& out) { return queue_.try_pop(out); }
  bool process_one_effect();
  std::size_t process_all_effects(std::size_t max_effects = 100'000);

  std::uint64_t state_hash() const;

private:
  void resolve();

  static constexpr Tick kTicksPerSecond = Tick{1} << 30;

  Tick tick_ = 0;
  Tick tick_rate_ = 1;
  Rng rng_;
  World world_{};
  EffectQueue queue_;
  EffectHandler effect_handler_{this};
  ScriptVM* script_ = nullptr; // non-owning
};

} // namespace arksim

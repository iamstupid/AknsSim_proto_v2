#pragma once

#include <cstdint>
#include <vector>

#include <flecs.h>

#include "sim_core/effect.hpp"
#include "sim_core/rng.hpp"
#include "sim_core/types.hpp"

namespace arksim {

class ScriptVM;

class SimState {
public:
  explicit SimState(std::uint64_t seed = 0);

  Tick tick() const { return tick_; }
  Rng& rng() { return rng_; }
  const Rng& rng() const { return rng_; }
  flecs::world& world() { return world_; }
  const flecs::world& world() const { return world_; }

  void set_tick_rate(Tick tick_rate);
  void set_tick_rate(std::uint64_t numerator, std::uint64_t denominator);
  float get_tick_rate_f32() const;
  double get_tick_rate_f64() const;

  void attach_script(ScriptVM* script);
  ScriptVM* script_vm() { return script_; }
  const ScriptVM* script_vm() const { return script_; }

  void step();
  void emit_effect(Effect effect);

  std::uint64_t state_hash() const;

private:
  void resolve();
  void commit(const std::vector<Effect>& effects);

  static constexpr Tick kTicksPerSecond = Tick{1} << 30;

  Tick tick_ = 0;
  Tick tick_rate_ = 1;
  std::uint32_t effect_seq_ = 0;
  Rng rng_;
  flecs::world world_{};
  EffectQueue queue_;
  ScriptVM* script_ = nullptr; // non-owning
};

} // namespace arksim

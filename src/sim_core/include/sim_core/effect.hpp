#pragma once

#include <cstdint>
#include <vector>

#include "sim_core/types.hpp"

namespace arksim {

enum class EffectKind : std::uint8_t {
  Damage = 0,
  Heal = 1,
  Move = 2,
  Spawn = 3,
  Despawn = 4,
  Custom = 255
};

struct Effect {
  Tick tick = 0;
  std::uint16_t priority = 0;
  UnitId src{};
  UnitId dst{};
  std::uint32_t seq = 0;
  EffectKind kind = EffectKind::Custom;
  float value = 0.0f;
};

struct EffectLess {
  bool operator()(const Effect& a, const Effect& b) const;
};

class EffectQueue {
public:
  void clear();
  void push(const Effect& effect);
  std::vector<Effect> drain_sorted();
  std::size_t size() const { return effects_.size(); }

private:
  std::vector<Effect> effects_;
};

} // namespace arksim

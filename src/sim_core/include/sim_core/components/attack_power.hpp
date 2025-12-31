#pragma once

#include "sim_core/core/types.hpp"

namespace arksim {

// Minimal offensive stat carrier.
// Other systems (skills, buffs, etc.) can modify `atk` and projectiles can either snapshot it at emit-time
// or read it live from the source entity at hit-time.
struct AttackPower {
  BuffNum atk{0.0};
};

} // namespace arksim


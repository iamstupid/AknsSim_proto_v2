#pragma once

#include <cstdint>

#include "sim_core/core/types.hpp"
#include "sim_core/ecs/ecs.hpp"

namespace arksim {

struct ArknightsLevel;
struct SimContext;
class SimState;

// Minimal stage runtime for driving Arknights `waves -> fragments -> SPAWN` schedules.
// Stores only snapshot-friendly runtime state; static level data lives in SimContext.
struct StageRuntime {
  bool active = false;
  Tick start_tick = 0;         // sim.tick() at stage start
  std::uint32_t next_spawn = 0; // index into ArknightsLevel::spawns

  int max_life_point = 0;
  int life_point = 0;
  int leaked = 0;

  void start(World& world, SimState& sim, SimContext& ctx, const ArknightsLevel& level);
  void step(World& world, SimState& sim, SimContext& ctx, const ArknightsLevel& level);
};

// Convenience helper: set ctx.map + ctx.arknights_level and (re)start StageRuntime on sim.world_entity().
void start_arknights_stage(SimState& sim, SimContext& ctx, const ArknightsLevel& level);

} // namespace arksim


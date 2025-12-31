#pragma once

#include <cstdint>
#include <vector>

#include "sim_core/components/attack_power.hpp"
#include "sim_core/components/damage.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/ecs/ecs.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/spatial/spatial_grid.hpp"
#include "sim_core/core/types.hpp"
#include "sim_core/core/vec.hpp"

namespace arksim {

class SimState;
class SpatialIndex;

struct Projectile {
  enum class HitShape : std::uint8_t {
    SingleTarget = 0,
    AoeCircle = 1,
    AoeTiles = 2,
  };

  enum class AttackPowerSettlement : std::uint8_t {
    // Cache the attack power at (or near) emit-time. For exact emit-time semantics, call
    // `settle_attack_power_snapshot(world)` right after creating the projectile.
    Snapshot = 0,

    // Damage amount always reads the current attack power from the source entity.
    BindToSource = 1,
  };

  bool active = true;
  bool emit_damage = true;
  bool consume_on_hit = true;

  // Only checked when a hit is triggered (interval gate).
  HitShape hit_shape = HitShape::SingleTarget;
  TypeFlags required = TypeFlags::Enemy;

  // If enabled, the damage amount is computed from the source entity's AttackPower:
  //   amount = dmg.amount + atk_multiplier * atk
  // and `atk` is either snapshotted or bound (see `attack_power_settlement`).
  bool use_source_attack_power = false;
  AttackPowerSettlement attack_power_settlement = AttackPowerSettlement::Snapshot;
  double atk_multiplier = 1.0;
  double attack_power_snapshot = 0.0;
  bool has_attack_power_snapshot = false;

  Damage dmg{};

  // Source entity handle (idx+gen). entity_id is resolved at runtime if needed.
  std::uint32_t source_idx = ecs_lab::kInvalidIndex;
  std::uint32_t source_gen = 0;

  // Optional: lock onto a specific target (idx+gen). When set, only that target can be hit.
  std::uint32_t target_idx = ecs_lab::kInvalidIndex;
  std::uint32_t target_gen = 0;

  // HitShape::AoeCircle
  f32 aoe_radius = 0.0f;

  // HitShape::AoeTiles (absolute tile list for now)
  std::vector<TileCoord> aoe_tiles{};

  // Periodic triggering: 0 means "every tick".
  Tick interval = 0;
  Tick interval_remain = 0;

  // Called once per hit target (after selection, before default damage emission).
  TriggerProcessor<World&, SimState*, Entity, Entity> OnHit;

  void settle_attack_power_snapshot(World& world);

  void set_source(Entity source);
  void clear_target();
  void set_target(Entity target);

  struct Scratch {
    std::vector<SpatialEntry> candidates;
    std::vector<Entity> hit_targets;
  };

  // Zero-allocation variant: caller owns scratch buffers.
  void step(World& world, SimState& sim, Entity self, const SpatialIndex& spatial, Tick tick_rate, Scratch& scratch);

  // Convenience wrapper (uses thread-local scratch).
  void step(World& world, SimState& sim, Entity self, const SpatialIndex& spatial, Tick tick_rate);
};

} // namespace arksim

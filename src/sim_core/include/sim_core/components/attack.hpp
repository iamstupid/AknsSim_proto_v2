#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sim_core/ecs.hpp"
#include "sim_core/target_selector.hpp"
#include "sim_core/types.hpp"

namespace arksim {

class SimState;

struct Attack {
  enum class State : std::uint8_t {
    Idle = 0,
    PreDelay = 1,
    PostDelay = 2,
    IntervalAwait = 3,
  };

  // Global clamp range for `atk_speed` (percent).
  // Defaults match typical Arknights-like constraints: [-95, 600].
  static inline float atk_speed_min = -95.0f;
  static inline float atk_speed_max = 600.0f;

  bool active = true;
  State state = State::Idle;

  // Target scanning interval. 0 means "scan every step".
  Tick scan_interval = 0;
  Tick scan_remain = 0;

  // Range selection config (used during scan and final fire-time select).
  TargetRange::Kind range_kind = TargetRange::Kind::Circle;
  TargetRange::Grid range_grid = TargetRange::Grid::Occupation;
  TypeFlags required = TypeFlags::Enemy;

  // Kind::Tiles
  std::vector<TileCoord> range_tiles{};

  // Kind::Circle: center is derived from Position.pos + center_offset.
  vec<f32> center_offset{};
  f32 range_radius = 0.0f;

  // If true and using Occupation grid, circle queries use `collect_circle_intersect` (considers entry radius).
  bool occupation_circle_intersect = false;

  // Arrangement config (filter + ordering) performed at fire-time.
  TargetArranger arranger{};

  // Attack speed bonus in percent (+k).
  // We avoid scaling base durations via division. Instead we scale the *elapsed* ticks:
  //
  //   scaled_elapsed = ceil(elapsed_ticks * (100 + atk_speed) / 100)
  //
  // and compare `scaled_elapsed` against `base_*` thresholds. This uses only integer math and
  // ensures attack timings are never longer than the base values when atk_speed >= 0.
  float atk_speed = 0.0f;

  // Base timings (ticks).
  Tick base_interval = 0; // t
  Tick base_pre = 0;      // t1
  Tick base_post = 0;     // t2

  // Runtime: phase progress.
  // Units are "tick * ceil((100 + atk_speed_clamped) * kMulScale)" so we can avoid division by (100 + atk_speed).
  // See helpers at the bottom of this type.
  std::uint64_t pre_progress = 0;
  std::uint64_t post_progress = 0;
  std::uint64_t interval_progress = 0;

  // Runtime: actual elapsed (scaled) ticks for current cycle phases (computed on transition).
  Tick pre_elapsed_scaled = 0;
  Tick post_elapsed_scaled = 0;

  // Runtime: interval_await target (scaled ticks) computed as:
  //   max(0, base_interval - pre_elapsed_scaled - post_elapsed_scaled)
  Tick interval_target = 0;

  // Cache updated by periodic scanning during Idle/PreDelay.
  std::vector<SpatialEntry> cached_candidates{};

  // Fire hook: called when PreDelay ends. Implementations can emit DamageEffect, spawn Projectile, etc.
  TriggerProcessor<World&, SimState*, Entity, std::span<const Entity>, Attack&> OnFire;

  bool allows_fsm_update() const { return state == State::Idle || state == State::IntervalAwait; }
  bool in_recovery() const { return state == State::PostDelay || state == State::IntervalAwait; }

  // Cancel PostDelay/IntervalAwait to Idle and immediately do a scan. If a target is found, enters PreDelay.
  bool cancel_recovery_and_rescan(World& world,
                                  SimState& sim,
                                  Entity self,
                                  const SpatialIndex& spatial,
                                  Tick tick_rate,
                                  TargetSelectorScratch& scratch);

  // Tick update. Note: this does not rebuild SpatialIndex; callers should rebuild after movement.
  void step(World& world,
            SimState& sim,
            Entity self,
            const SpatialIndex& spatial,
            Tick tick_rate,
            TargetSelectorScratch& scratch);

private:
  bool scan_(World& world, Entity self, const SpatialIndex& spatial, TargetSelectorScratch& scratch, bool update_cache);

  void reset_to_idle_(bool force_scan);
  void enter_pre_delay_();
  void enter_post_delay_();
  void enter_interval_await_();
  void fire_(World& world, SimState& sim, Entity self, TargetSelectorScratch& scratch);

  static constexpr std::uint32_t kMulScale = 1024;
  static constexpr std::uint64_t kMulDen = 100ull * static_cast<std::uint64_t>(kMulScale);

  static std::uint32_t atk_speed_mul_(float atk_speed);
  static std::uint64_t phase_threshold_den_(Tick target_scaled_ticks);
  static Tick ceil_div_den_(std::uint64_t x);
  static void add_progress_(std::uint64_t& progress, Tick tick_rate, std::uint32_t mul);
  static bool timer_due_(Tick& remain, Tick interval, Tick tick_rate);
};

} // namespace arksim

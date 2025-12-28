#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sim_core/components/spatial.hpp"
#include "sim_core/ecs.hpp"
#include "sim_core/map.hpp"
#include "sim_core/spatial_grid.hpp"
#include "sim_core/types.hpp"
#include "sim_core/vec.hpp"

namespace arksim {

struct TargetKey {
  double a = 0.0;
  double b = 0.0;
};

struct TargetScoredEntry {
  SpatialEntry entry{};
  TargetKey key{};
};

struct TargetSelectorScratch;

struct TargetRange {
  enum class Kind : std::uint8_t {
    Tiles = 0,
    Circle = 1,
  };

  enum class Grid : std::uint8_t {
    Occupation = 0,
    Center = 1,
  };

  Kind kind = Kind::Tiles;
  Grid grid = Grid::Occupation;
  TypeFlags required = TypeFlags::None;

  // Kind::Tiles
  std::span<const TileCoord> tiles{};

  // Kind::Circle
  vec<f32> center{};
  f32 radius = 0.0f;

  void collect(const SpatialIndex& spatial, std::vector<SpatialEntry>& out) const;
};

struct TargetArranger {
  enum class Relationship : std::uint8_t {
    Unknown = 0,
    Friendly = 1,
    Hostile = 2,
  };

  enum class Primary : std::uint8_t {
    None = 0,
    CreatedTimeAsc,
    CreatedTimeDesc,
    DistToSourceAsc,
    DistToSourceDesc,
    HpRatioAsc,
    HpRatioDesc,
  };

  enum class Secondary : std::uint8_t {
    None = 0,
    FlyFirst,
    RangedApplywayFirst,
    MeleeApplywayFirst,
    SpecifiedFilterTag,
    SpecifiedBuff,
    SpecifiedBuffPairOr,
  };

  Primary primary = Primary::CreatedTimeAsc;
  bool prefer_blocked = false;
  Secondary secondary = Secondary::None;

  // Target selectability / semantics (see targetSelector.wiki).
  Relationship relationship = Relationship::Unknown;
  bool ignore_unselectable = false; // Invisible/Invincible
  bool ignore_camouflage = false;
  bool is_heal = false; // filters NonHeal
  bool exclude_destroyed = true;

  // Remove candidates which contain any of these flags.
  TypeFlags excluded = TypeFlags::None;

  // 0 means "no limit".
  std::uint32_t max_targets = 1;

  // Optional: used by some keys/filters (distance-to-source, specified tag/buff, etc).
  Entity source{};
  std::uint64_t param = 0;

  void arrange(World& world, std::span<const SpatialEntry> candidates, TargetSelectorScratch& scratch) const;
};

struct TargetSelectorScratch {
  std::vector<SpatialEntry> candidates;
  std::vector<TargetScoredEntry> scored;
  std::vector<Entity> targets;
};

struct TargetSelector {
  TargetRange range;
  TargetArranger arranger;

  std::span<const Entity> select(World& world, const SpatialIndex& spatial, TargetSelectorScratch& scratch) const;
};

} // namespace arksim

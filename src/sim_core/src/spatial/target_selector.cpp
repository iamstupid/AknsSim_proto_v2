#include "sim_core/spatial/target_selector.hpp"

#include <algorithm>
#include <cmath>

#include "sim_core/components/destroyed.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/components/position.hpp"

namespace arksim {
namespace {

constexpr bool has_any_flags(TypeFlags v, TypeFlags mask) {
  return any(v & mask);
}

bool key_lt(const TargetKey& a, const TargetKey& b) {
  if (a.a != b.a) {
    return a.a < b.a;
  }
  return a.b < b.b;
}

TargetKey compute_key(World& world, const SpatialEntry& e, const TargetArranger& arranger) {
  switch (arranger.primary) {
    case TargetArranger::Primary::None:
      return {};

    case TargetArranger::Primary::CreatedTimeAsc:
      return TargetKey{static_cast<double>(e.entity.entity_id), 0.0};

    case TargetArranger::Primary::CreatedTimeDesc:
      return TargetKey{-static_cast<double>(e.entity.entity_id), 0.0};

    case TargetArranger::Primary::DistToSourceAsc:
    case TargetArranger::Primary::DistToSourceDesc: {
      vec<f32> src_pos{};
      if (const auto* p = world.try_get<Position>(arranger.source)) {
        src_pos = p->pos;
      }
      const double dx = static_cast<double>(e.center.x) - static_cast<double>(src_pos.x);
      const double dy = static_cast<double>(e.center.y) - static_cast<double>(src_pos.y);
      const double d2 = dx * dx + dy * dy;
      const double sign = (arranger.primary == TargetArranger::Primary::DistToSourceDesc) ? -1.0 : 1.0;
      return TargetKey{sign * d2, 0.0};
    }

    case TargetArranger::Primary::HpRatioAsc:
    case TargetArranger::Primary::HpRatioDesc: {
      double ratio = 1.0;
      if (const auto* hp = world.try_get<HP>(e.entity)) {
        ratio = hp->ratio;
      }
      const double sign = (arranger.primary == TargetArranger::Primary::HpRatioDesc) ? -1.0 : 1.0;
      return TargetKey{sign * ratio, 0.0};
    }
  }

  return {};
}

template <typename Pred>
void apply_secondary_swap(std::vector<TargetScoredEntry>& v, Pred&& pred) {
  std::size_t first_not = 0;
  while (first_not < v.size() && pred(v[first_not])) {
    ++first_not;
  }

  for (std::size_t i = first_not + 1; i < v.size(); ++i) {
    if (!pred(v[i])) {
      continue;
    }
    std::swap(v[i], v[first_not]);
    ++first_not;
    while (first_not < v.size() && pred(v[first_not])) {
      ++first_not;
    }
  }
}

} // namespace

void TargetRange::collect(const SpatialIndex& spatial, std::vector<SpatialEntry>& out) const {
  const SpatialGrid* g = nullptr;
  switch (grid) {
    case Grid::Occupation:
      g = &spatial.occupation;
      break;
    case Grid::Center:
      g = &spatial.center;
      break;
  }

  if (!g) {
    out.clear();
    return;
  }

  switch (kind) {
    case Kind::Tiles:
      g->collect_tiles(tiles, required, out);
      break;
    case Kind::Circle:
      g->collect_circle(center, radius, required, out);
      break;
  }
}

void TargetArranger::arrange(World& world, std::span<const SpatialEntry> candidates, TargetSelectorScratch& scratch) const {
  scratch.targets.clear();
  scratch.scored.clear();
  if (candidates.empty()) {
    return;
  }

  scratch.scored.reserve(candidates.size());
  for (const SpatialEntry& e : candidates) {
    if (exclude_destroyed) {
      if (!world.is_alive(e.entity) || world.has<Destroyed>(e.entity)) {
        continue;
      }
    }
    if (any(excluded) && has_any_flags(e.flags, excluded)) {
      continue;
    }
    if (is_heal && has_any_flags(e.flags, TypeFlags::NonHeal)) {
      continue;
    }
    if (relationship == Relationship::Hostile) {
      if (!ignore_unselectable && has_any_flags(e.flags, TypeFlags::Invisible | TypeFlags::Invincible)) {
        continue;
      }
      if (!ignore_camouflage && has_any_flags(e.flags, TypeFlags::Camouflage)) {
        continue;
      }
    }
    scratch.scored.push_back(TargetScoredEntry{e, compute_key(world, e, *this)});
  }
  if (scratch.scored.empty()) {
    return;
  }

  std::sort(scratch.scored.begin(), scratch.scored.end(), [](const TargetScoredEntry& a, const TargetScoredEntry& b) {
    if (key_lt(a.key, b.key)) {
      return true;
    }
    if (key_lt(b.key, a.key)) {
      return false;
    }
    return a.entry.entity.entity_id < b.entry.entity.entity_id;
  });

  if (prefer_blocked) {
    apply_secondary_swap(scratch.scored,
                         [](const TargetScoredEntry& e) { return has_any_flags(e.entry.flags, TypeFlags::Blocked); });
  }

  switch (secondary) {
    case Secondary::None:
      break;
    case Secondary::FlyFirst:
      apply_secondary_swap(scratch.scored,
                           [](const TargetScoredEntry& e) { return has_any_flags(e.entry.flags, TypeFlags::Air); });
      break;
    case Secondary::RangedApplywayFirst:
      apply_secondary_swap(scratch.scored,
                           [](const TargetScoredEntry& e) { return has_any_flags(e.entry.flags, TypeFlags::Ranged); });
      break;
    case Secondary::MeleeApplywayFirst:
      apply_secondary_swap(scratch.scored,
                           [](const TargetScoredEntry& e) { return has_any_flags(e.entry.flags, TypeFlags::Melee); });
      break;
    case Secondary::SpecifiedFilterTag:
    case Secondary::SpecifiedBuff:
    case Secondary::SpecifiedBuffPairOr:
      // Not implemented yet: requires tag/buff runtime data.
      break;
  }

  const std::size_t n =
      (max_targets == 0) ? scratch.scored.size() : std::min<std::size_t>(scratch.scored.size(), max_targets);
  scratch.targets.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    scratch.targets.push_back(scratch.scored[i].entry.entity);
  }
}

std::span<const Entity> TargetSelector::select(World& world, const SpatialIndex& spatial, TargetSelectorScratch& scratch) const {
  range.collect(spatial, scratch.candidates);
  arranger.arrange(world, scratch.candidates, scratch);
  return std::span<const Entity>(scratch.targets.data(), scratch.targets.size());
}

} // namespace arksim

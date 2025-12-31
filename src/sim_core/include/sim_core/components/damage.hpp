#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "sim_core/ecs/ecs.hpp"
#include "sim_core/core/rng.hpp"
#include "sim_core/core/types.hpp"

namespace arksim {

class SimState;

struct Damage {
  using TypeMask = std::uint32_t;

  enum Type : TypeMask {
    physical = 1u << 0,
    arts = 1u << 1,
    elemental = 1u << 2,
    true_damage = 1u << 3,
    uncond = 1u << 4,
    ignore_dodge = 1u << 5,
    melee = 1u << 6,
    ranged = 1u << 7,
    splash = 1u << 8
  };

  static constexpr TypeMask GENERAL = ignore_dodge | melee | ranged | splash;
  static constexpr TypeMask ALL = physical | arts | elemental | true_damage | uncond | GENERAL;
  static constexpr TypeMask ALL_NO_DODGE = physical | arts | elemental | true_damage | uncond | melee | ranged | splash;

  double amount = 0.0;
  TypeMask type = 0;

  bool is_zero() const;
};

struct DamageProcessor {
  enum class Kind : std::uint8_t {
    None = 0,
    Arts,
    Physical,
    Elemental,
    Dodge,
    Rewrite,
    Multiplier,
    DamageReduction,
    Barrier,
    Shield,
    LuaCustom
  };

  Damage::TypeMask processed_types = 0;
  double value = 0.0;
  Kind kind = Kind::None;
  Entity source{};
  int lua_func_ref = -1;

  bool matches(const Damage& dmg) const;
  double apply(World& world, SimState* sim, Entity self, bool purity, double dmg) const;
};

constexpr int kDefaultRewritePriority = 100000;

struct DamageAggregator {
  struct Entry {
    int priority = 0;
    std::uint64_t name = 0;
    DamageProcessor proc;
  };

  DamageAggregator() = default;

  DamageAggregator(const DamageAggregator& other)
      : procs(other.procs) {
    // `sorted` caches pointers into `procs`; it must be rebuilt in the copy.
    dirty = true;
  }

  DamageAggregator& operator=(const DamageAggregator& other) {
    if (this == &other) {
      return *this;
    }
    procs = other.procs;
    sorted.clear();
    dirty = true;
    return *this;
  }

  DamageAggregator(DamageAggregator&&) noexcept = default;
  DamageAggregator& operator=(DamageAggregator&&) noexcept = default;

  void add(Entity name, int priority, DamageProcessor proc);
  void add(std::uint64_t name, int priority, DamageProcessor proc);
  void add(Entity name, DamageProcessor proc);
  void add(std::uint64_t name, DamageProcessor proc);
  void erase(Entity name);
  void erase(std::uint64_t name);
  bool update_value(Entity name, double value);
  bool update_value(std::uint64_t name, double value);
  bool update_priority(Entity name, int priority);
  bool update_priority(std::uint64_t name, int priority);
  void clear();

  Damage operator()(World& world, SimState* sim, Entity self, Damage dmg);
  Damage try_do_damage(World& world, SimState* sim, Entity self, Damage dmg);

  std::map<std::uint64_t, Entry> procs;
  std::vector<const Entry*> sorted;

private:
  Damage apply(World& world, SimState* sim, Entity self, bool purity, Damage dmg);
  void rebuild_sorted_if_needed();

  bool dirty = false;
};

struct DefStats {
  BuffNum def;
  BuffNum res;
  BuffNum eres;
  DamageAggregator dagr;
  TriggerProcessor<World&, Entity, Damage> OnDodge;
  TriggerProcessor<World&, Entity, Entity, Damage> OnDamage;
  TriggerProcessor<World&, Entity, Entity, Damage> OnHit;
};

void make_hit(World& world, SimState* sim, Entity from, Entity to, Damage dmg);

// Default DefStats wiring:
// - installs base (arts/phys/elem) processors
// - installs a high-priority OnDamage hook to apply damage to HP (if present)
constexpr std::uint64_t kDefStatsArtsProcName = 0x617274735f70726full;    // "arts_pro"
constexpr std::uint64_t kDefStatsPhysProcName = 0x706879735f70726full;    // "phys_pro"
constexpr std::uint64_t kDefStatsElemProcName = 0x656c656d5f70726full;    // "elem_pro"
constexpr std::uint64_t kDefStatsApplyHpOnDamageName = 0x6170706c795f6870ull; // "apply_hp"
constexpr int kDefStatsApplyHpOnDamagePriority = 1'000'000;

void init_defstats(DefStats& stats);
DefStats& ensure_defstats(World& world, Entity self);

DamageProcessor make_arts_proc();
DamageProcessor make_phys_proc();
DamageProcessor make_elem_proc();
DamageProcessor make_dodge_proc(Damage::TypeMask mask = Damage::ALL_NO_DODGE);
DamageProcessor make_rewrite_proc(double value, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_multiplier_proc(double multiplier, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_damage_reduction_proc(double reduction, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_barrier_proc(Entity barrier_entity, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_shield_proc(Entity shield_entity, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_lua_custom_proc(int lua_func_ref,
                                     Damage::TypeMask mask = Damage::ALL,
                                     Entity source = {});

} // namespace arksim

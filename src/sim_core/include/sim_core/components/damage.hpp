#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <flecs.h>

#include "sim_core/rng.hpp"
#include "sim_core/types.hpp"

namespace arksim {

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
  flecs::entity source{};
  int lua_func_ref = -1;

  bool matches(const Damage& dmg) const;
  double apply(flecs::entity self, bool purity, double dmg) const;
};

constexpr int kDefaultRewritePriority = 100000;

struct DamageAggregator {
  struct Entry {
    int priority = 0;
    std::uint64_t name = 0;
    DamageProcessor proc;
  };

  void add(flecs::entity name, int priority, DamageProcessor proc);
  void add(std::uint64_t name, int priority, DamageProcessor proc);
  void add(flecs::entity name, DamageProcessor proc);
  void add(std::uint64_t name, DamageProcessor proc);
  void erase(flecs::entity name);
  void erase(std::uint64_t name);
  bool update_value(flecs::entity name, double value);
  bool update_value(std::uint64_t name, double value);
  bool update_priority(flecs::entity name, int priority);
  bool update_priority(std::uint64_t name, int priority);
  void clear();

  Damage operator()(flecs::entity self, Damage dmg);
  Damage try_do_damage(flecs::entity self, Damage dmg);

  std::map<std::uint64_t, Entry> procs;
  std::vector<const Entry*> sorted;

private:
  Damage apply(flecs::entity self, bool purity, Damage dmg);
  void rebuild_sorted_if_needed();

  bool dirty = false;
};

struct DefStats {
  BuffNum def;
  BuffNum res;
  BuffNum eres;
  DamageAggregator dagr;
  TriggerProcessor<flecs::entity, Damage> OnDodge;
  TriggerProcessor<flecs::entity, flecs::entity, Damage> OnDamage;
  TriggerProcessor<flecs::entity, flecs::entity, Damage> OnHit;
};

void make_hit(flecs::entity from, flecs::entity to, Damage dmg);

DamageProcessor make_arts_proc();
DamageProcessor make_phys_proc();
DamageProcessor make_elem_proc();
DamageProcessor make_dodge_proc(Damage::TypeMask mask = Damage::ALL_NO_DODGE);
DamageProcessor make_rewrite_proc(double value, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_multiplier_proc(double multiplier, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_damage_reduction_proc(double reduction, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_barrier_proc(flecs::entity barrier_entity, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_shield_proc(flecs::entity shield_entity, Damage::TypeMask mask = Damage::ALL);
DamageProcessor make_lua_custom_proc(int lua_func_ref,
                                     Damage::TypeMask mask = Damage::ALL,
                                     flecs::entity source = {});

} // namespace arksim

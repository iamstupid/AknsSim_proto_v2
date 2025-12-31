#include "sim_core/components/damage.hpp"

#include <algorithm>
#include <utility>

#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/scripting/script_vm.hpp"
#include "sim_core/runtime/sim_state.hpp"

namespace arksim {

bool Damage::is_zero() const {
  return amount == 0.0;
}

bool DamageProcessor::matches(const Damage& dmg) const {
  return (processed_types & dmg.type) == dmg.type;
}

double DamageProcessor::apply(World& world, SimState* sim, Entity self, bool purity, double dmg) const {
  switch (kind) {
    case Kind::Arts: {
      const auto* stats = world.try_get<DefStats>(self);
      if (!stats) {
        return dmg;
      }
      const double ratio = std::clamp(1.0 - static_cast<double>(stats->res), 0.05, 1.0);
      return dmg * ratio;
    }
    case Kind::Physical: {
      const auto* stats = world.try_get<DefStats>(self);
      if (!stats) {
        return dmg;
      }
      const double def = static_cast<double>(stats->def);
      const double reduced = dmg - def;
      const double min_value = 0.05 * dmg;
      return std::clamp(reduced, min_value, dmg);
    }
    case Kind::Elemental: {
      const auto* stats = world.try_get<DefStats>(self);
      if (!stats) {
        return dmg;
      }
      const double ratio = std::clamp(1.0 - static_cast<double>(stats->eres), 0.05, 1.0);
      return dmg * ratio;
    }
    case Kind::Dodge: {
      if (purity) {
        return dmg;
      }
      if (!world.is_alive(self)) {
        return dmg;
      }
      auto* stats = world.try_get<DefStats>(self);
      if (!stats) {
        return dmg;
      }
      if (!sim) {
        return dmg;
      }
      if (sim->rng().next_f64() < value) {
        stats->OnDodge(world, self, Damage{dmg, 0});
        return 0.0;
      }
      return dmg;
    }
    case Kind::Rewrite:
      return value;
    case Kind::Multiplier:
      return value * dmg;
    case Kind::DamageReduction:
      return dmg * (1.0 - value);
    case Kind::Barrier: {
      if (!world.is_alive(source)) {
        return dmg;
      }
      auto* barrier = world.try_get<Barrier>(source);
      if (!barrier) {
        return dmg;
      }
      const double discount = std::min(barrier->amount, dmg);
      const double next = dmg - discount;
      if (!purity) {
        barrier->amount -= discount;
        if (barrier->amount < 0.0) {
          barrier->amount = 0.0;
        }
      }
      return next;
    }
    case Kind::Shield: {
      if (!world.is_alive(source)) {
        return dmg;
      }
      auto* shield = world.try_get<Shield>(source);
      if (!shield) {
        return dmg;
      }
      if (shield->hp > 0) {
        if (!purity) {
          --shield->hp;
        }
        return 0.0;
      }
      return dmg;
    }
    case Kind::LuaCustom: {
      if (lua_func_ref < 0) {
        return dmg;
      }
      if (!sim) {
        return dmg;
      }
      auto* vm = sim->script_vm();
      if (!vm) {
        return dmg;
      }
      double result = dmg;
      if (!vm->call_damage_processor(lua_func_ref,
                                     self.entity_id,
                                     purity,
                                     value,
                                     dmg,
                                     source.entity_id,
                                     result)) {
        return dmg;
      }
      return result;
    }
    case Kind::None:
    default:
      return dmg;
  }
}

void DamageAggregator::add(Entity name, int priority, DamageProcessor proc) {
  add(entity_key(name), priority, std::move(proc));
}

void DamageAggregator::add(std::uint64_t name, int priority, DamageProcessor proc) {
  auto it = procs.find(name);
  if (it != procs.end()) {
    if (proc.value <= it->second.proc.value) {
      return;
    }
  }

  Entry entry;
  entry.priority = priority;
  entry.name = name;
  entry.proc = std::move(proc);
  procs[name] = std::move(entry);
  dirty = true;
}

void DamageAggregator::add(Entity name, DamageProcessor proc) {
  add(entity_key(name), std::move(proc));
}

void DamageAggregator::add(std::uint64_t name, DamageProcessor proc) {
  const int priority = (proc.kind == DamageProcessor::Kind::Rewrite) ? kDefaultRewritePriority : 0;
  add(name, priority, std::move(proc));
}

void DamageAggregator::erase(Entity name) {
  erase(entity_key(name));
}

void DamageAggregator::erase(std::uint64_t name) {
  procs.erase(name);
  dirty = true;
}

bool DamageAggregator::update_value(Entity name, double value) {
  return update_value(entity_key(name), value);
}

bool DamageAggregator::update_value(std::uint64_t name, double value) {
  auto it = procs.find(name);
  if (it == procs.end()) {
    return false;
  }
  it->second.proc.value = value;
  return true;
}

bool DamageAggregator::update_priority(Entity name, int priority) {
  return update_priority(entity_key(name), priority);
}

bool DamageAggregator::update_priority(std::uint64_t name, int priority) {
  auto it = procs.find(name);
  if (it == procs.end()) {
    return false;
  }
  if (it->second.priority == priority) {
    return true;
  }
  it->second.priority = priority;
  dirty = true;
  return true;
}

void DamageAggregator::clear() {
  procs.clear();
  sorted.clear();
  dirty = false;
}

Damage DamageAggregator::operator()(World& world, SimState* sim, Entity self, Damage dmg) {
  return apply(world, sim, self, false, dmg);
}

Damage DamageAggregator::try_do_damage(World& world, SimState* sim, Entity self, Damage dmg) {
  return apply(world, sim, self, true, dmg);
}

Damage DamageAggregator::apply(World& world, SimState* sim, Entity self, bool purity, Damage dmg) {
  if (dmg.is_zero() || procs.empty()) {
    return dmg;
  }

  rebuild_sorted_if_needed();
  for (const Entry* entry : sorted) {
    if (dmg.amount == 0.0) {
      break;
    }
    const DamageProcessor& proc = entry->proc;
    if (!proc.matches(dmg)) {
      continue;
    }
    dmg.amount = proc.apply(world, sim, self, purity, dmg.amount);
  }

  return dmg;
}

void DamageAggregator::rebuild_sorted_if_needed() {
  if (!dirty) {
    return;
  }

  sorted.clear();
  sorted.reserve(procs.size());
  for (auto& entry : procs) {
    sorted.push_back(&entry.second);
  }

  std::stable_sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) {
    if (a->priority != b->priority) {
      return a->priority < b->priority;
    }
    return a->name < b->name;
  });

  dirty = false;
}

void make_hit(World& world, SimState* sim, Entity from, Entity to, Damage dmg) {
  auto* stats = world.try_get<DefStats>(to);
  if (!stats) {
    return;
  }

  stats->OnHit(world, to, from, dmg);
  dmg = stats->dagr(world, sim, to, dmg);
  if (!dmg.is_zero()) {
    stats->OnDamage(world, to, from, dmg);
  }
}

void init_defstats(DefStats& stats) {
  // Ensure base mitigation processors exist.
  stats.dagr.add(kDefStatsArtsProcName, make_arts_proc());
  stats.dagr.add(kDefStatsPhysProcName, make_phys_proc());
  stats.dagr.add(kDefStatsElemProcName, make_elem_proc());

  // HP is applied via an OnDamage hook so other triggers can observe/modify first.
  if (!stats.OnDamage.contains(kDefStatsApplyHpOnDamageName)) {
    stats.OnDamage.add(kDefStatsApplyHpOnDamageName,
                       kDefStatsApplyHpOnDamagePriority,
                       [](World& world, Entity self, Entity from, Damage dmg) {
                         do_damage(world, self, dmg.amount, from);
                       });
  }
}

DefStats& ensure_defstats(World& world, Entity self) {
  DefStats* stats = world.try_get<DefStats>(self);
  if (!stats) {
    DefStats init;
    init.def = BuffNum(0.0);
    init.res = BuffNum(0.0);
    init.eres = BuffNum(0.0);
    stats = &world.add<DefStats>(self, init);
  }
  init_defstats(*stats);
  return *stats;
}

DamageProcessor make_arts_proc() {
  DamageProcessor proc;
  proc.processed_types = Damage::arts | Damage::GENERAL;
  proc.kind = DamageProcessor::Kind::Arts;
  return proc;
}

DamageProcessor make_phys_proc() {
  DamageProcessor proc;
  proc.processed_types = Damage::physical | Damage::GENERAL;
  proc.kind = DamageProcessor::Kind::Physical;
  return proc;
}

DamageProcessor make_elem_proc() {
  DamageProcessor proc;
  proc.processed_types = Damage::elemental | Damage::GENERAL;
  proc.kind = DamageProcessor::Kind::Elemental;
  return proc;
}

DamageProcessor make_dodge_proc(Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.kind = DamageProcessor::Kind::Dodge;
  return proc;
}

DamageProcessor make_rewrite_proc(double value, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.value = value;
  proc.kind = DamageProcessor::Kind::Rewrite;
  return proc;
}

DamageProcessor make_multiplier_proc(double multiplier, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.value = multiplier;
  proc.kind = DamageProcessor::Kind::Multiplier;
  return proc;
}

DamageProcessor make_damage_reduction_proc(double reduction, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.value = reduction;
  proc.kind = DamageProcessor::Kind::DamageReduction;
  return proc;
}

DamageProcessor make_barrier_proc(Entity barrier_entity, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.source = barrier_entity;
  proc.kind = DamageProcessor::Kind::Barrier;
  return proc;
}

DamageProcessor make_shield_proc(Entity shield_entity, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.source = shield_entity;
  proc.kind = DamageProcessor::Kind::Shield;
  return proc;
}

DamageProcessor make_lua_custom_proc(int lua_func_ref, Damage::TypeMask mask, Entity source) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.kind = DamageProcessor::Kind::LuaCustom;
  proc.lua_func_ref = lua_func_ref;
  proc.source = source;
  return proc;
}

} // namespace arksim

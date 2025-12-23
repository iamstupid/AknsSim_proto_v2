#include "sim_core/components/damage.hpp"

#include <algorithm>
#include <utility>

#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/script_vm.hpp"
#include "sim_core/sim_state.hpp"

namespace arksim {

bool Damage::is_zero() const {
  return amount == 0.0;
}

bool DamageProcessor::matches(const Damage& dmg) const {
  return (processed_types & dmg.type) == dmg.type;
}

double DamageProcessor::apply(flecs::entity self, bool purity, double dmg) const {
  switch (kind) {
    case Kind::Arts: {
      const auto* stats = self.try_get<DefStats>();
      if (!stats) {
        return dmg;
      }
      const double ratio = std::clamp(1.0 - static_cast<double>(stats->res), 0.05, 1.0);
      return dmg * ratio;
    }
    case Kind::Physical: {
      const auto* stats = self.try_get<DefStats>();
      if (!stats) {
        return dmg;
      }
      const double def = static_cast<double>(stats->def);
      const double reduced = dmg - def;
      const double min_value = 0.05 * dmg;
      return std::clamp(reduced, min_value, dmg);
    }
    case Kind::Elemental: {
      const auto* stats = self.try_get<DefStats>();
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
      if (!self.is_alive()) {
        return dmg;
      }
      auto* stats = self.try_get_mut<DefStats>();
      if (!stats) {
        return dmg;
      }
      auto* sim = static_cast<SimState*>(self.world().get_ctx());
      if (!sim) {
        return dmg;
      }
      if (sim->rng().next_f64() < value) {
        stats->OnDodge(self, Damage{dmg, 0});
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
      if (!source.is_alive()) {
        return dmg;
      }
      auto* barrier = source.try_get_mut<Barrier>();
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
        source.modified<Barrier>();
      }
      return next;
    }
    case Kind::Shield: {
      if (!source.is_alive()) {
        return dmg;
      }
      auto* shield = source.try_get_mut<Shield>();
      if (!shield) {
        return dmg;
      }
      if (shield->hp > 0) {
        if (!purity) {
          --shield->hp;
          source.modified<Shield>();
        }
        return 0.0;
      }
      return dmg;
    }
    case Kind::LuaCustom: {
      if (lua_func_ref < 0) {
        return dmg;
      }
      auto* sim = static_cast<SimState*>(self.world().get_ctx());
      if (!sim) {
        return dmg;
      }
      auto* vm = sim->script_vm();
      if (!vm) {
        return dmg;
      }
      double result = dmg;
      if (!vm->call_damage_processor(lua_func_ref, self.id(), purity, value, dmg, source.id(), result)) {
        return dmg;
      }
      return result;
    }
    case Kind::None:
    default:
      return dmg;
  }
}

void DamageAggregator::add(flecs::entity name, int priority, DamageProcessor proc) {
  add(name.id(), priority, std::move(proc));
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

void DamageAggregator::add(flecs::entity name, DamageProcessor proc) {
  add(name.id(), std::move(proc));
}

void DamageAggregator::add(std::uint64_t name, DamageProcessor proc) {
  const int priority = (proc.kind == DamageProcessor::Kind::Rewrite) ? kDefaultRewritePriority : 0;
  add(name, priority, std::move(proc));
}

void DamageAggregator::erase(flecs::entity name) {
  erase(name.id());
}

void DamageAggregator::erase(std::uint64_t name) {
  procs.erase(name);
  dirty = true;
}

bool DamageAggregator::update_value(flecs::entity name, double value) {
  return update_value(name.id(), value);
}

bool DamageAggregator::update_value(std::uint64_t name, double value) {
  auto it = procs.find(name);
  if (it == procs.end()) {
    return false;
  }
  it->second.proc.value = value;
  return true;
}

bool DamageAggregator::update_priority(flecs::entity name, int priority) {
  return update_priority(name.id(), priority);
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

Damage DamageAggregator::operator()(flecs::entity self, Damage dmg) {
  return apply(self, false, dmg);
}

Damage DamageAggregator::try_do_damage(flecs::entity self, Damage dmg) {
  return apply(self, true, dmg);
}

Damage DamageAggregator::apply(flecs::entity self, bool purity, Damage dmg) {
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
    dmg.amount = proc.apply(self, purity, dmg.amount);
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

void make_hit(flecs::entity from, flecs::entity to, Damage dmg) {
  auto* stats = to.try_get_mut<DefStats>();
  if (!stats) {
    return;
  }

  stats->OnHit(to, from, dmg);
  dmg = stats->dagr(to, dmg);
  if (!dmg.is_zero()) {
    stats->OnDamage(to, from, dmg);
  }

  to.modified<DefStats>();
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

DamageProcessor make_barrier_proc(flecs::entity barrier_entity, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.source = barrier_entity;
  proc.kind = DamageProcessor::Kind::Barrier;
  return proc;
}

DamageProcessor make_shield_proc(flecs::entity shield_entity, Damage::TypeMask mask) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.source = shield_entity;
  proc.kind = DamageProcessor::Kind::Shield;
  return proc;
}

DamageProcessor make_lua_custom_proc(int lua_func_ref, Damage::TypeMask mask, flecs::entity source) {
  DamageProcessor proc;
  proc.processed_types = mask;
  proc.kind = DamageProcessor::Kind::LuaCustom;
  proc.lua_func_ref = lua_func_ref;
  proc.source = source;
  return proc;
}

} // namespace arksim

#pragma once

#include <utility>

#include "sim_core/ecs/ecs.hpp"
#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/damage.hpp"
#include "sim_core/components/hp.hpp"

namespace arksim {

class SimState;

struct WorldApi {
  World& world;
  SimState* sim = nullptr;

  explicit WorldApi(World& world_ref, SimState* sim_ref = nullptr)
      : world(world_ref), sim(sim_ref) {}

  template <typename Fn, typename... Args>
  decltype(auto) call(Fn&& fn, Args&&... args) {
    return std::forward<Fn>(fn)(world, std::forward<Args>(args)...);
  }

  template <typename Fn, typename... Args>
  decltype(auto) call_with_sim(Fn&& fn, Args&&... args) {
    return std::forward<Fn>(fn)(world, sim, std::forward<Args>(args)...);
  }

#define WORLD_FN(name, ret, params, ...) \
  ret name params { return ::arksim::name(world, __VA_ARGS__); }
#define SIM_FN(name, ret, params, ...) \
  ret name params { return ::arksim::name(world, sim, __VA_ARGS__); }
#include "sim_core/scripting/world_api.def"
#undef SIM_FN
#undef WORLD_FN
};

} // namespace arksim

# ArkSim Proto v2 Design Notes

## Scope and goals
- Focus on `sim_core` first. `sim_game` is a test harness for a long time.
- Deterministic simulation on x86_64 using floating-point math.
- No multithreading and no WASM targets in the initial phase.
- Use LuaJIT for scripting and ECS-Lab for ECS.

## Project layout
- `src/sim_core`: simulation core library (time, RNG, ECS, components).
- `apps/sim_cli`: CLI entry point (data and batch-style workflows).
- `apps/sim_game`: SDL-based game host (currently minimal).
- `tests`: doctest-based unit tests.
- `ECS-Lab`: ECS submodule (custom ECS implementation).

## Build and dependencies
- CMake with presets (`x64-debug`, `x64-release`, etc.).
- vcpkg toolchain for dependencies.
- Dependencies:
  - ECS-Lab (ECS)
  - LuaJIT (scripting)
  - SDL2 (game host)
  - nlohmann-json (data)
  - doctest (tests)

## Time model
- `Tick` is a 64-bit integer count since start.
- 1 tick = 1 / 2^30 seconds.
- `tick_rate` defines how many ticks a single `step()` advances.
- `set_tick_rate(numerator, denominator)` configures seconds per step via ceil(numerator * 2^30 / denominator).

## Determinism
- Deterministic ordering is a first-class constraint for tests and replays.
- `EffectQueue` drains effects in FIFO order.
- RNG uses xoshiro256** with splitmix64 seeding for reproducibility.

## ECS integration
- `SimState` owns an ECS-Lab `World` and exposes it to systems/components.
- Components are plain C++ structs stored on ECS-Lab entities.

## Script integration
- `ScriptVM` holds a LuaJIT state and binds a `SimState` as its world.
- Script entry point: `OnTick(tick)` (Lua global).
- Hot reload is supported by re-loading the script file and re-binding `OnTick`.

## Core components

### Buff
- Tracks giver, targets, and `life_remain`.
- `step()` decrements life and triggers `OnDestruct` when expiring.
- `OnDestruct` is used to clean up linked processors on the target.

### HP
- `total_hp` uses `BuffNum` for layered modifiers.
- `ratio` keeps current HP as a fraction of `total_hp`.
- `do_damage` and `do_heal` emit overflow/underflow events.

### Damage and defense
- `Damage` carries amount and a type mask.
- `DamageAggregator` holds ordered `DamageProcessor` entries.
- `DefStats` owns the aggregator plus triggers: `OnHit`, `OnDamage`, `OnDodge`.
- `make_hit` sequence: OnHit -> aggregator -> OnDamage.

### Barrier and Shield
- Built on top of `Buff` and `DefStats` processors.
- Barrier reduces damage by subtracting from `amount`.
- Shield consumes charges and can optionally regenerate/decay over time.

## Testing
- `tests/test_sim_core.cpp` covers RNG, time, effect ordering, BuffNum,
  TriggerProcessor ordering, HP, and barrier/shield flows.

## Near-term follow-ups
- Add more gameplay components and JSON schemas.
- Define save/load boundaries once state format stabilizes.
- Expand LuaJIT bindings for ECS and components.

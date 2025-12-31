# sim_core Guide

`sim_core` is the deterministic simulation core.

## SimState
- Owns RNG, tick counters, effect queue, ECS world, and script VM pointer.
- Tick model: 1 tick = 1 / 2^30 seconds.
- `tick_rate` defines how many ticks are advanced per `step()`.

Key APIs:
- `step()` advances the simulation by `tick_rate`.
- `step_frame(ctx)` runs the core frame pipeline (movement -> spatial -> combat -> effects) and advances time.
- `state_hash()` returns a stable hash of core state.
- `world()` exposes ECS-Lab world.
- `world_api()` exposes WorldApi with auto-forwarding helpers.

## SimContext
`SimContext` holds non-ECS simulation state used by multiple systems:
- `Map` + pathing caches (`BresenhamCache`, `PathMapCache`)
- `SpatialIndex` (two spatial grids)
- reusable scratch buffers (e.g. target selection)

## Effects
`EffectQueue` is FIFO and is consumed one effect at a time (see `SimState::try_pop_effect`).
The `Effect` payload is a compact struct containing:
- `type` (8-bit)
- `fnidx` (24-bit)
- `src` (entity_idx)
- `param_buffer` (24 bytes)

## RNG
Uses xoshiro256** for reproducibility. Seeded from a 64-bit value.

## Determinism Rules
- Avoid non-deterministic iteration order over unordered containers.
- All randomness must go through `Rng`.

# Scripting Guide (LuaJIT)

This project uses LuaJIT for scripting.

## ScriptVM
- `ScriptVM` owns a Lua state.
- `bind_world(SimState*)` exposes simulation state to scripts.
- `load_file()` loads a script file and caches `OnTick`.
- `call_on_tick(tick)` calls the Lua `OnTick` function if present.

## Recommended Usage
- Scripts should be deterministic: do not use Lua RNG; use sim RNG bindings.
- Keep script-side state minimal; prefer ECS data in C++.
- Hot reload by reloading the file and re-binding functions.

## Determinism sandbox
The default `ScriptVM` environment is restricted to reduce non-determinism:
- `require` is available for **pure Lua** modules, and module search is restricted to the loaded script's directory.
- Native module loading is disabled (`package.cpath` is cleared, and `package.loadlib` is removed).
- Host-dependent libraries are not exposed (`io`, `os`, `debug`, `jit`).
- `math.random` / `math.randomseed` are removed; use sim RNG bindings instead.

## TODO
- Define the exact Lua API exposed to scripts (entities, components, effects).

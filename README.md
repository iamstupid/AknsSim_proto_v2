# ArkSim Proto v2

This repository contains the core simulation (`sim_core`), a CLI harness, a minimal SDL host, and a custom ECS submodule (ECS-Lab).

## Quick Start (Windows + VS Toolchain)

1) Initialize submodules:
```powershell
git submodule update --init --recursive
```

2) Configure (vcpkg toolchain):
```powershell
cmake --preset x64-debug
```

3) Build:
```powershell
cmake --build out/build/x64-debug
```

To build the minimal SDL host (`sim_game`), configure with:
```powershell
cmake --preset x64-debug -DARKSIM_BUILD_GAME=ON
```

4) Test:
```powershell
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

If `cmake` or `ctest` are not in PATH, you can use the VS CMake bundle:
```
D:\vs community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
D:\vs community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe
```

## Project Layout
- `src/sim_core` : simulation core library
- `apps/sim_cli` : CLI entry point
- `apps/sim_game`: SDL host (minimal)
- `tests`       : doctest-based unit tests
- `ECS-Lab`     : ECS submodule (see `ECS-Lab/docs/ecs_lab_api.md`)
- `docs`        : design and usage docs

## Documentation Index
- `docs/design.md` : high-level design notes
- `docs/build.md` : build + dependency setup
- `docs/tooling.md` : clangd/VS/vcpkg setup
- `docs/ecs_migration.md` : ECS-Lab integration notes
- `docs/sim_core.md` : simulation core concepts
- `docs/components.md` : gameplay component behaviors
- `docs/scripting.md` : LuaJIT integration notes
- `docs/testing.md` : tests and stress tests
- `docs/data.md` : data formats (placeholder)

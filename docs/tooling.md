# Tooling Guide

This document captures local tooling expectations for editor and build integration.

## clangd
- Compile database: `out/build/x64-debug/compile_commands.json`
- Ensure C++20 is used for ECS-Lab headers.
- If headers are not in the compile DB, use the header TU in `ECS-Lab/src/ecs_lab_headers.cpp`.

Recommended `.clangd` settings:
- Add include paths for `src/sim_core/include` and `ECS-Lab/include`.
- Force `-std=c++20`.

## Visual Studio
The repository assumes VS toolchains are available. If PATH does not include cmake/ctest, use:
```
$Env:VSINSTALLDIR\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
$Env:VSINSTALLDIR\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe
```

## vcpkg
Dependencies are managed via `vcpkg.json`. If you add new dependencies, update that file.

## Submodules
ECS-Lab is a submodule. After clone:
```powershell
git submodule update --init --recursive
```

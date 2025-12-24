# Build Guide

This project uses CMake + vcpkg with Visual Studio toolchains on Windows.

## Prerequisites
- Visual Studio (with C++ toolchain)
- CMake (VS bundles a CMake build)
- vcpkg (as the dependency provider)

## Configure
Use presets (recommended):
```powershell
cmake --preset x64-debug
```

If you need to specify the vcpkg toolchain manually:
```powershell
cmake -S . -B out/build/x64-debug -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

## Build
```powershell
cmake --build out/build/x64-debug
```

## Test
```powershell
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

If `cmake`/`ctest` are not in PATH, use the VS bundle:
```
D:\vs community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
D:\vs community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe
```

## Common Issues
- "ctest not found": use the VS CMake bundle path above.
- "std headers not found": run the build inside a VS Developer Command Prompt (or call `VsDevCmd.bat`).
- "submodule missing": run `git submodule update --init --recursive`.

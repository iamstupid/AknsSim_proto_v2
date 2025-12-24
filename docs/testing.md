# Testing Guide

Tests use doctest and live under `tests/`.

## Run tests
```powershell
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

## ECS-Lab tests
ECS-Lab has its own test suite in the submodule:
- `ECS-Lab/tests/test_ecs_lab.cpp`
- `ECS-Lab/tests/bench_signature.cpp`

## Stress Testing
The ECS-Lab tests include a randomized stress case for entity add/remove/destroy.
Increase scale by adjusting the constants in the stress test if needed.

## Guidelines
- Keep tests deterministic.
- Avoid reliance on wall-clock timing.
- Prefer small reproducible seeds for random tests.

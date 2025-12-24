# ECS-Lab Integration Notes

This project uses the ECS-Lab submodule instead of Flecs.

## Key Changes vs Flecs
- `SimState` owns an `ecs_lab::World`.
- All component functions receive `World&` explicitly or use `WorldApi` wrappers.
- Entities are plain handles (`entity_id`, `entity_idx`, `gen`).
- No built-in context pointer on `World`.

## World API Helper
Use `SimState::world_api()` or `WorldApi` to avoid passing `World&` everywhere.

Example:
```cpp
auto api = sim.world_api();
api.do_damage(target, 10.0, source);
api.make_hit(attacker, target, dmg); // auto-injects world + sim
```

## Submodule Usage
Update ECS-Lab:
```powershell
git submodule update --remote --merge
```

## Documentation
Refer to ECS-Lab docs:
- `ECS-Lab/docs/ecs_lab_api.md`

# Components Guide

This document summarizes the current gameplay components in `sim_core`.

## BuffNum
Layered modifier type:
- direct add/mult
- final add/mult
- `value()` computes the combined result

## HP
- `total_hp` (BuffNum)
- `ratio` (0..1)
- `OnUnderflow` / `OnOverflow` triggers

Helper functions:
- `do_damage(world, entity, amount, source)`
- `do_heal(world, entity, amount, source)`

## Damage / DefStats
- `Damage` = (amount, type mask)
- `DamageProcessor` applies a transform based on kind
- `DamageAggregator` holds ordered processors
- `DefStats` includes `dagr` and triggers: `OnHit`, `OnDamage`, `OnDodge`

Sequence:
```
OnHit -> DamageAggregator -> OnDamage
```

## Buff
Tracks:
- giver
- targets
- life_remain
- `OnDestruct`

## Barrier / Shield
Both are buffs and can inject damage processors:
- Barrier: consumes an amount of damage
- Shield: consumes hit counts, optional regen/decay

## Notes
Many components assume a live `World` and should not be called with stale entities.

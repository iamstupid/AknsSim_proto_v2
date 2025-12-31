#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "sim_core/effects/effect.hpp"
#include "sim_core/ecs/destroyed.hpp"
#include "sim_core/effects/effects.hpp"
#include "sim_core/ecs/entity_component.hpp"
#include "sim_core/components/area.hpp"
#include "sim_core/components/attack.hpp"
#include "sim_core/components/attack_power.hpp"
#include "sim_core/components/block.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/damage.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/components/projectile.hpp"
#include "sim_core/components/route_move.hpp"
#include "sim_core/components/spatial.hpp"
#include "sim_core/components/unbalance.hpp"
#include "sim_core/nav/bresenham_cache.hpp"
#include "sim_core/nav/map.hpp"
#include "sim_core/nav/path_map.hpp"
#include "sim_core/core/rng.hpp"
#include "sim_core/runtime/sim_context.hpp"
#include "sim_core/runtime/sim_state.hpp"
#include "sim_core/spatial/spatial_grid.hpp"
#include "sim_core/spatial/target_selector.hpp"
#include "sim_core/core/vec.hpp"

TEST_CASE("Rng deterministic") {
  arksim::Rng r1(12345);
  arksim::Rng r2(12345);

  for (int i = 0; i < 8; ++i) {
    CHECK(r1.next_u32() == r2.next_u32());
  }
}

TEST_CASE("Rng uniform ranges") {
  arksim::Rng rng(42);

  for (int i = 0; i < 1000; ++i) {
    const auto v = rng.uniform_u32(10, 20);
    CHECK(v >= 10);
    CHECK(v <= 20);

    const auto s = rng.uniform_i32(-5, 5);
    CHECK(s >= -5);
    CHECK(s <= 5);

    const auto f = rng.uniform_f32(-1.0f, 1.0f);
    CHECK(f >= -1.0f);
    CHECK(f < 1.0f);
  }
}

TEST_CASE("Effect FIFO ordering") {
  arksim::EffectQueue queue;

  arksim::Effect a;
  a.type = 1;
  a.src = 1;

  arksim::Effect b;
  b.type = 2;
  b.src = 1;

  arksim::Effect c;
  c.type = 3;
  c.src = 1;

  queue.push(a);
  queue.push(b);
  queue.push(c);

  arksim::Effect out;
  REQUIRE(queue.try_pop(out));
  CHECK(out.type == 1);
  REQUIRE(queue.try_pop(out));
  CHECK(out.type == 2);
  REQUIRE(queue.try_pop(out));
  CHECK(out.type == 3);
}

TEST_CASE("SimState hash stable") {
  arksim::SimState s1(777);
  arksim::SimState s2(777);

  for (int i = 0; i < 100; ++i) {
    s1.step();
    s2.step();
  }

  CHECK(s1.state_hash() == s2.state_hash());
}

TEST_CASE("SimState+SimContext snapshot restore (script excluded)") {
  arksim::SimState sim(999);
  arksim::SimContext ctx;
  ctx.reset_map(8, 8);

  const arksim::TileCoord tile{1, 1};
  CHECK(ctx.map.flags(tile) == arksim::TileFlags::None);

  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  world.add<arksim::Position>(attacker, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});

  arksim::Entity target = world.create();
  world.add<arksim::Position>(target, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});
  world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(100.0);
  hp.ratio = 1.0;
  world.add<arksim::HP>(target, hp);
  arksim::ensure_defstats(world, target);

  arksim::Attack atk;
  atk.scan_interval = 0;
  atk.scan_remain = 0;
  atk.range_kind = arksim::TargetRange::Kind::Circle;
  atk.range_grid = arksim::TargetRange::Grid::Occupation;
  atk.required = arksim::TypeFlags::Enemy;
  atk.range_radius = 1.0f;
  atk.base_interval = 10;
  atk.base_pre = 2;
  atk.base_post = 1;
  atk.arranger.max_targets = 1;

  int fired = 0;
  atk.OnFire.add(1, 0, [&](arksim::World&, arksim::SimState* s, arksim::Entity self, std::span<const arksim::Entity> ts, arksim::Attack&) {
    ++fired;
    REQUIRE(s != nullptr);
    REQUIRE(ts.size() == 1);
    const double dmg_amount = static_cast<double>(s->rng().uniform_u32(10, 20));
    arksim::Damage dmg{dmg_amount, arksim::Damage::physical};
    s->emit_effect(arksim::make_damage_effect(self, ts[0], dmg));
  });

  world.add<arksim::Attack>(attacker, atk);

  // Run some frames so that the attack fires and consumes RNG.
  sim.step_frame(ctx);
  sim.step_frame(ctx);
  sim.step_frame(ctx);
  CHECK(fired >= 1);

  const auto snap_sim = sim.snapshot();
  const auto snap_ctx = ctx.snapshot();

  const double ratio_at_snap = world.get<arksim::HP>(target).ratio;
  const arksim::Tick tick_at_snap = sim.tick();
  const int fired_at_snap = fired;

  // Mutate map/world further.
  ctx.map.set_flag(tile, arksim::TileFlags::Obstacle, true);
  CHECK(ctx.map.flags(tile) == arksim::TileFlags::Obstacle);

  for (int i = 0; i < 20; ++i) {
    sim.step_frame(ctx);
  }
  const double ratio_after = world.get<arksim::HP>(target).ratio;
  const arksim::Tick tick_after = sim.tick();
  const int fired_after = fired;

  // Restore and re-run the same number of frames: results should match exactly.
  ctx.restore(snap_ctx);
  sim.restore(snap_sim);
  fired = fired_at_snap;

  CHECK(sim.tick() == tick_at_snap);
  CHECK(world.get<arksim::HP>(target).ratio == doctest::Approx(ratio_at_snap));
  CHECK(fired == fired_at_snap);
  CHECK(ctx.map.flags(tile) == arksim::TileFlags::None);

  for (int i = 0; i < 20; ++i) {
    sim.step_frame(ctx);
  }

  CHECK(sim.tick() == tick_after);
  CHECK(world.get<arksim::HP>(target).ratio == doctest::Approx(ratio_after));
  CHECK(fired == fired_after);
}

TEST_CASE("SimState tick rate") {
  arksim::SimState sim(1);
  sim.set_tick_rate(3);
  sim.step();
  CHECK(sim.tick() == 3);

  sim.set_tick_rate(1, 1ull << 30); // 1 tick = 1/(2^30) seconds
  CHECK(sim.get_tick_rate_f32() == doctest::Approx(1.0f / (1u << 30)));
  sim.step();
  CHECK(sim.tick() == 4);
}

TEST_CASE("BuffNum value") {
  arksim::BuffNum num(100.0);
  num.direct_add = 10.0;
  num.direct_mult = 0.2; // +20%
  num.final_add = 5.0;
  num.final_mult = 1.5;

  const double expected = ((100.0 + 10.0) * 1.2 + 5.0) * 1.5;
  CHECK(static_cast<double>(num) == doctest::Approx(expected));
}

TEST_CASE("BuffNum helpers") {
  arksim::BuffNum num(50.0);
  num.add_direct_add(5.0);
  num.add_direct_mult(0.1);
  num.add_final_add(2.0);
  num.mul_final_mult(2.0);
  CHECK(static_cast<double>(num) == doctest::Approx(((50.0 + 5.0) * 1.1 + 2.0) * 2.0));

  num.clear_modifiers();
  CHECK(static_cast<double>(num) == doctest::Approx(50.0));

  num.reset(10.0);
  CHECK(static_cast<double>(num) == doctest::Approx(10.0));
}

TEST_CASE("TriggerProcessor ordering") {
  arksim::TriggerProcessor<int> proc;
  std::vector<int> calls;

  proc.add(10, 1, [&](int v) { calls.push_back(v + 10); });
  proc.add(2, 0, [&](int v) { calls.push_back(v + 2); });
  proc.add(5, 0, [&](int v) { calls.push_back(v + 5); });

  proc(1);
  REQUIRE(calls.size() == 3);
  CHECK(calls[0] == 3);  // priority 0, name 2
  CHECK(calls[1] == 6);  // priority 0, name 5
  CHECK(calls[2] == 11); // priority 1, name 10

  calls.clear();
  proc.erase(5);
  proc(2);
  REQUIRE(calls.size() == 2);
  CHECK(calls[0] == 4);
  CHECK(calls[1] == 12);
}

TEST_CASE("EntityComponent destroy flow") {
  arksim::World world;
  arksim::Entity e = world.create();

  struct DemoComponent : arksim::EntityComponent {
    int value = 0;
  } comp;

  comp.bind(world, e);

  bool intercepted = false;
  comp.OnDestroy.add(1, 0, [&](arksim::World&, arksim::Entity, arksim::EntityComponent& c) {
    if (!intercepted) {
      c.destroyed = false;
      intercepted = true;
    }
  });

  arksim::Destroy(comp);
  CHECK(world.is_alive(e));

  arksim::Destroy(comp);
  CHECK(world.is_alive(e));
  CHECK(world.has<arksim::Destroyed>(e));
  arksim::cleanup_destroyed(world);
  CHECK(!world.is_alive(e));
}

TEST_CASE("HP damage/heal triggers") {
  arksim::World world;
  arksim::Entity target = world.create();
  arksim::Entity source = world.create();

  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(100.0);
  hp.ratio = 1.0;

  bool underflow_called = false;
  bool overflow_called = false;

  hp.OnUnderflow.add(1, 0, [&](arksim::World&, arksim::Entity, arksim::Entity, double amount) {
    underflow_called = true;
    CHECK(amount == doctest::Approx(10.0));
  });

  hp.OnOverflow.add(2, 0, [&](arksim::World&, arksim::Entity, arksim::Entity, double amount) {
    overflow_called = true;
    CHECK(amount == doctest::Approx(15.0));
  });

  world.add<arksim::HP>(target, hp);

  arksim::do_damage(world, target, 110.0, source);
  CHECK(underflow_called);

  arksim::do_heal(world, target, 125.0, source);
  CHECK(overflow_called);
}

TEST_CASE("DamageEffect applies HP via DefStats OnDamage hook") {
  arksim::SimState sim(123);
  arksim::World& world = sim.world();

  arksim::Entity src = world.create();
  arksim::Entity dst = world.create();

  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(100.0);
  hp.ratio = 1.0;
  world.add<arksim::HP>(dst, hp);
  arksim::ensure_defstats(world, dst);

  arksim::Damage dmg;
  dmg.amount = 25.0;
  dmg.type = arksim::Damage::physical;
  sim.emit_effect(arksim::make_damage_effect(src, dst, dmg));

  CHECK(sim.process_one_effect());
  CHECK(world.get<arksim::HP>(dst).ratio == doctest::Approx(0.75));
  CHECK(!sim.process_one_effect());
}

TEST_CASE("SpatialGrid circle_intersect considers entry radius") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Area area;
  area.type = arksim::Area::Type::Circle;
  area.radius = arksim::vec<arksim::f32>{0.3f, 0.0f};
  world.add<arksim::Area>(e, area);

  arksim::Spatial spatial_tag;
  spatial_tag.flags = arksim::TypeFlags::Enemy;
  world.add<arksim::Spatial>(e, spatial_tag);

  arksim::SpatialIndex spatial;
  spatial.reset(5, 5);
  spatial.rebuild(world);

  std::vector<arksim::SpatialEntry> out;

  // Center distance is 0.4, query radius is 0.2 => center-only query excludes the entity.
  spatial.occupation.collect_circle(arksim::vec<arksim::f32>{0.4f, 0.0f}, 0.2f, arksim::TypeFlags::Enemy, out);
  CHECK(out.empty());

  // But with radius intersection (0.2 + 0.3 >= 0.4), it should be included.
  spatial.occupation.collect_circle_intersect(arksim::vec<arksim::f32>{0.4f, 0.0f}, 0.2f, arksim::TypeFlags::Enemy, out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].entity.entity_id == e.entity_id);
}

TEST_CASE("Projectile emits DamageEffect on collision") {
  arksim::SimState sim(7);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  arksim::Entity target = world.create();

  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(100.0);
  hp.ratio = 1.0;
  world.add<arksim::HP>(target, hp);
  arksim::ensure_defstats(world, target);

  arksim::Position tgt_pos;
  tgt_pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(target, tgt_pos);

  arksim::Area tgt_area;
  tgt_area.type = arksim::Area::Type::Circle;
  tgt_area.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
  world.add<arksim::Area>(target, tgt_area);

  arksim::Spatial tgt_spatial;
  tgt_spatial.flags = arksim::TypeFlags::Enemy;
  world.add<arksim::Spatial>(target, tgt_spatial);

  arksim::Entity proj_e = world.create();
  arksim::Position proj_pos;
  proj_pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  proj_pos.set_step_mode(arksim::Position::StepMode::Stop);
  world.add<arksim::Position>(proj_e, proj_pos);

  arksim::Area proj_area;
  proj_area.type = arksim::Area::Type::Circle;
  proj_area.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
  world.add<arksim::Area>(proj_e, proj_area);

  arksim::Projectile proj;
  proj.required = arksim::TypeFlags::Enemy;
  proj.dmg.amount = 10.0;
  proj.dmg.type = arksim::Damage::physical;
  proj.set_source(attacker);
  world.add<arksim::Projectile>(proj_e, proj);

  arksim::SpatialIndex spatial;
  spatial.reset(5, 5);
  spatial.rebuild(world);

  const arksim::Tick one_second = arksim::Tick{1} << 30;
  world.get<arksim::Projectile>(proj_e).step(world, sim, proj_e, spatial, one_second);

  CHECK(world.has<arksim::Destroyed>(proj_e));
  CHECK(sim.process_one_effect());
  CHECK(world.get<arksim::HP>(target).ratio == doctest::Approx(0.9));
}

TEST_CASE("Projectile attack power settlement: snapshot vs bind") {
  arksim::SimState sim(9);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  world.add<arksim::AttackPower>(attacker, arksim::AttackPower{arksim::BuffNum(100.0)});

  arksim::Entity target = world.create();
  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(200.0);
  hp.ratio = 1.0;
  world.add<arksim::HP>(target, hp);
  arksim::ensure_defstats(world, target);

  arksim::Position tgt_pos;
  tgt_pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(target, tgt_pos);

  arksim::Area tgt_area;
  tgt_area.type = arksim::Area::Type::Circle;
  tgt_area.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
  world.add<arksim::Area>(target, tgt_area);

  arksim::Spatial tgt_spatial;
  tgt_spatial.flags = arksim::TypeFlags::Enemy;
  world.add<arksim::Spatial>(target, tgt_spatial);

  arksim::SpatialIndex spatial;
  spatial.reset(5, 5);

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  SUBCASE("Snapshot") {
    arksim::Entity proj_e = world.create();
    world.add<arksim::Position>(proj_e, arksim::Position{});
    world.add<arksim::Area>(proj_e, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});

    arksim::Projectile proj;
    proj.required = arksim::TypeFlags::Enemy;
    proj.use_source_attack_power = true;
    proj.attack_power_settlement = arksim::Projectile::AttackPowerSettlement::Snapshot;
    proj.atk_multiplier = 1.0;
    proj.dmg.amount = 0.0;
    proj.dmg.type = arksim::Damage::physical;
    proj.set_source(attacker);
    proj.settle_attack_power_snapshot(world); // settle at emit-time
    world.add<arksim::Projectile>(proj_e, proj);

    // Change attacker atk after projectile creation; snapshot should keep 100.
    world.get<arksim::AttackPower>(attacker).atk.base = 50.0;

    spatial.rebuild(world);
    world.get<arksim::Projectile>(proj_e).step(world, sim, proj_e, spatial, one_second);
    REQUIRE(sim.process_one_effect());
    CHECK(world.get<arksim::HP>(target).ratio == doctest::Approx(0.5)); // 200 - 100
  }

  SUBCASE("BindToSource") {
    arksim::Entity proj_e = world.create();
    world.add<arksim::Position>(proj_e, arksim::Position{});
    world.add<arksim::Area>(proj_e, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});

    arksim::Projectile proj;
    proj.required = arksim::TypeFlags::Enemy;
    proj.use_source_attack_power = true;
    proj.attack_power_settlement = arksim::Projectile::AttackPowerSettlement::BindToSource;
    proj.atk_multiplier = 1.0;
    proj.dmg.amount = 0.0;
    proj.dmg.type = arksim::Damage::physical;
    proj.set_source(attacker);
    world.add<arksim::Projectile>(proj_e, proj);

    // Modify attacker atk before hit; bind should see the new value.
    world.get<arksim::AttackPower>(attacker).atk.base = 50.0;

    spatial.rebuild(world);
    world.get<arksim::Projectile>(proj_e).step(world, sim, proj_e, spatial, one_second);
    REQUIRE(sim.process_one_effect());
    CHECK(world.get<arksim::HP>(target).ratio == doctest::Approx(0.75)); // 200 - 50
  }
}

TEST_CASE("Attack basic cycle (scan -> pre -> fire -> post -> interval -> idle)") {
  arksim::SimState sim(11);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  arksim::Position atk_pos;
  atk_pos.pos = arksim::vec<arksim::f32>{2.0f, 2.0f};
  world.add<arksim::Position>(attacker, atk_pos);

  arksim::Entity target = world.create();
  arksim::Position tgt_pos;
  tgt_pos.pos = arksim::vec<arksim::f32>{2.0f, 2.0f};
  world.add<arksim::Position>(target, tgt_pos);
  world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

  arksim::SpatialIndex spatial;
  spatial.reset(8, 8);
  spatial.rebuild(world);

  arksim::Attack atk;
  atk.scan_interval = 1;
  atk.scan_remain = 0;
  atk.range_kind = arksim::TargetRange::Kind::Circle;
  atk.range_grid = arksim::TargetRange::Grid::Occupation;
  atk.required = arksim::TypeFlags::Enemy;
  atk.range_radius = 1.0f;
  atk.base_interval = 10;
  atk.base_pre = 2;
  atk.base_post = 3;
  atk.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeAsc;
  atk.arranger.max_targets = 1;

  int fired = 0;
  atk.OnFire.add(1, 0, [&](arksim::World&, arksim::SimState*, arksim::Entity self, std::span<const arksim::Entity> ts, arksim::Attack&) {
    ++fired;
    CHECK(self.entity_id == attacker.entity_id);
    REQUIRE(ts.size() == 1);
    CHECK(ts[0].entity_id == target.entity_id);
  });

  world.add<arksim::Attack>(attacker, atk);
  auto& a = world.get<arksim::Attack>(attacker);

  arksim::TargetSelectorScratch scratch;
  const arksim::Tick one_tick = 1;

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PreDelay);
  CHECK(a.pre_progress == 0);
  CHECK(fired == 0);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PreDelay);
  CHECK(a.pre_progress == 102400);
  CHECK(a.pre_elapsed_scaled == 0);
  CHECK(fired == 0);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PostDelay);
  CHECK(a.pre_elapsed_scaled == 2);
  CHECK(fired == 1);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PostDelay);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PostDelay);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::IntervalAwait);
  CHECK(a.post_elapsed_scaled == 3);
  CHECK(a.interval_target == 5); // 10 - 2 - 3

  for (int i = 0; i < 5; ++i) {
    a.step(world, sim, attacker, spatial, one_tick, scratch);
  }
  CHECK(a.state == arksim::Attack::State::Idle);
}

TEST_CASE("Attack pre-delay resets to idle if targets lost") {
  arksim::SimState sim(12);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  world.add<arksim::Position>(attacker, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});

  arksim::Entity target = world.create();
  world.add<arksim::Position>(target, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});
  world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

  arksim::SpatialIndex spatial;
  spatial.reset(8, 8);
  spatial.rebuild(world);

  arksim::Attack atk;
  atk.scan_interval = 1;
  atk.scan_remain = 0;
  atk.required = arksim::TypeFlags::Enemy;
  atk.range_radius = 1.0f;
  atk.base_interval = 10;
  atk.base_pre = 5;
  atk.base_post = 0;
  atk.arranger.max_targets = 1;
  world.add<arksim::Attack>(attacker, atk);
  auto& a = world.get<arksim::Attack>(attacker);

  arksim::TargetSelectorScratch scratch;
  const arksim::Tick one_tick = 1;

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PreDelay);

  // Target disappears from spatial: remove Spatial tag and rebuild index.
  world.remove<arksim::Spatial>(target);
  spatial.rebuild(world);

  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::Idle);
}

TEST_CASE("Attack atk_speed scales elapsed ticks (ceil, no division by 100+aspd)") {
  arksim::SimState sim(14);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  world.add<arksim::Position>(attacker, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});

  arksim::Entity target = world.create();
  world.add<arksim::Position>(target, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});
  world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

  arksim::SpatialIndex spatial;
  spatial.reset(8, 8);
  spatial.rebuild(world);

  arksim::Attack atk;
  atk.scan_interval = 0; // scan every step
  atk.scan_remain = 0;
  atk.required = arksim::TypeFlags::Enemy;
  atk.range_kind = arksim::TargetRange::Kind::Circle;
  atk.range_grid = arksim::TargetRange::Grid::Occupation;
  atk.range_radius = 1.0f;
  atk.base_interval = 1000;
  atk.base_pre = 10;
  atk.base_post = 0;
  atk.atk_speed = 100.0f; // +100% => should take ~half ticks
  atk.arranger.max_targets = 1;

  int fired = 0;
  atk.OnFire.add(1, 0, [&](arksim::World&, arksim::SimState*, arksim::Entity, std::span<const arksim::Entity>, arksim::Attack&) {
    ++fired;
  });

  world.add<arksim::Attack>(attacker, atk);
  auto& a = world.get<arksim::Attack>(attacker);

  arksim::TargetSelectorScratch scratch;
  const arksim::Tick one_tick = 1;

  // Scan -> PreDelay (no time passes for PreDelay on this tick).
  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PreDelay);
  CHECK(fired == 0);

  int elapsed_ticks = 0;
  while (fired == 0 && elapsed_ticks < 20) {
    a.step(world, sim, attacker, spatial, one_tick, scratch);
    ++elapsed_ticks;
  }

  REQUIRE(fired == 1);
  CHECK(elapsed_ticks == 5);
  CHECK(a.pre_elapsed_scaled == 10);
}

TEST_CASE("Attack atk_speed clamps to global bounds") {
  CHECK(arksim::Attack::atk_speed_min == doctest::Approx(-95.0f));
  CHECK(arksim::Attack::atk_speed_max == doctest::Approx(600.0f));

  struct Guard {
    float min_old = arksim::Attack::atk_speed_min;
    float max_old = arksim::Attack::atk_speed_max;
    Guard(float min_new, float max_new) {
      arksim::Attack::atk_speed_min = min_new;
      arksim::Attack::atk_speed_max = max_new;
    }
    ~Guard() {
      arksim::Attack::atk_speed_min = min_old;
      arksim::Attack::atk_speed_max = max_old;
    }
  };

  auto ticks_until_fire = [](float atk_speed, arksim::Tick base_pre) {
    arksim::SimState sim(15);
    arksim::World& world = sim.world();

    arksim::Entity attacker = world.create();
    world.add<arksim::Position>(attacker, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});

    arksim::Entity target = world.create();
    world.add<arksim::Position>(target, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});
    world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
    world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

    arksim::SpatialIndex spatial;
    spatial.reset(8, 8);
    spatial.rebuild(world);

    arksim::Attack atk;
    atk.scan_interval = 0;
    atk.scan_remain = 0;
    atk.required = arksim::TypeFlags::Enemy;
    atk.range_kind = arksim::TargetRange::Kind::Circle;
    atk.range_grid = arksim::TargetRange::Grid::Occupation;
    atk.range_radius = 1.0f;
    atk.base_interval = 1000;
    atk.base_pre = base_pre;
    atk.base_post = 0;
    atk.atk_speed = atk_speed;
    atk.arranger.max_targets = 1;

    int fired = 0;
    atk.OnFire.add(1, 0, [&](arksim::World&, arksim::SimState*, arksim::Entity, std::span<const arksim::Entity>, arksim::Attack&) {
      ++fired;
    });

    world.add<arksim::Attack>(attacker, atk);
    auto& a = world.get<arksim::Attack>(attacker);

    arksim::TargetSelectorScratch scratch;
    const arksim::Tick one_tick = 1;

    a.step(world, sim, attacker, spatial, one_tick, scratch); // scan -> PreDelay
    REQUIRE(a.state == arksim::Attack::State::PreDelay);

    int elapsed_ticks = 0;
    while (fired == 0 && elapsed_ticks < 500) {
      a.step(world, sim, attacker, spatial, one_tick, scratch);
      ++elapsed_ticks;
    }

    REQUIRE(fired == 1);
    return elapsed_ticks;
  };

  SUBCASE("High values clamp to +600%") {
    CHECK(ticks_until_fire(1.0e9f, 10) == 2);
  }

  SUBCASE("Low values clamp to -95%") {
    CHECK(ticks_until_fire(-1.0e9f, 2) == 21);
  }

  SUBCASE("Global override affects clamping") {
    Guard guard(0.0f, 0.0f);
    CHECK(ticks_until_fire(1.0e9f, 5) == 5);
  }
}

TEST_CASE("Attack cancel recovery -> idle + rescan") {
  arksim::SimState sim(13);
  arksim::World& world = sim.world();

  arksim::Entity attacker = world.create();
  world.add<arksim::Position>(attacker, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});

  arksim::Entity target = world.create();
  world.add<arksim::Position>(target, arksim::Position{arksim::vec<arksim::f32>{2.0f, 2.0f}});
  world.add<arksim::Area>(target, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(target, arksim::Spatial{arksim::TypeFlags::Enemy});

  arksim::SpatialIndex spatial;
  spatial.reset(8, 8);
  spatial.rebuild(world);

  arksim::Attack atk;
  atk.scan_interval = 1;
  atk.scan_remain = 0;
  atk.required = arksim::TypeFlags::Enemy;
  atk.range_radius = 1.0f;
  atk.base_interval = 10;
  atk.base_pre = 1;
  atk.base_post = 10;
  atk.arranger.max_targets = 1;

  int fired = 0;
  atk.OnFire.add(1, 0, [&](arksim::World&, arksim::SimState*, arksim::Entity, std::span<const arksim::Entity>, arksim::Attack&) {
    ++fired;
  });

  world.add<arksim::Attack>(attacker, atk);
  auto& a = world.get<arksim::Attack>(attacker);

  arksim::TargetSelectorScratch scratch;
  const arksim::Tick one_tick = 1;

  // Scan -> PreDelay
  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PreDelay);

  // Fire -> PostDelay
  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(a.state == arksim::Attack::State::PostDelay);
  CHECK(fired == 1);

  // Cancel recovery and immediately rescan -> PreDelay
  REQUIRE(a.cancel_recovery_and_rescan(world, sim, attacker, spatial, one_tick, scratch));
  CHECK(a.state == arksim::Attack::State::PreDelay);

  // Next tick fires again.
  a.step(world, sim, attacker, spatial, one_tick, scratch);
  CHECK(fired == 2);
}

TEST_CASE("Buff life countdown") {
  arksim::World world;
  arksim::Entity buff_entity = world.create();
  arksim::Buff buff;
  buff.life_remain = 10;
  buff.step(world, buff_entity, 4);
  CHECK(world.is_alive(buff_entity));
  buff.step(world, buff_entity, 4);
  CHECK(world.is_alive(buff_entity));
  buff.step(world, buff_entity, 4);
  CHECK(world.is_alive(buff_entity));
  CHECK(world.has<arksim::Destroyed>(buff_entity));
  arksim::cleanup_destroyed(world);
  CHECK(!world.is_alive(buff_entity));
}

TEST_CASE("Barrier and Shield usage") {
  arksim::World world;
  arksim::Entity target = world.create();
  arksim::DefStats stats{arksim::BuffNum(0.0), arksim::BuffNum(0.0), arksim::BuffNum(0.0)};
  world.add<arksim::DefStats>(target, stats);

  arksim::Entity barrier_entity = arksim::make_barrier(world, target, 5.0, 0.0);
  auto& barrier = world.get<arksim::Barrier>(barrier_entity);
  barrier.UseBarrier(3.0);
  CHECK(barrier.amount == doctest::Approx(2.0));
  CHECK(world.is_alive(barrier_entity));
  barrier.UseBarrier(3.0);
  CHECK(barrier.amount == doctest::Approx(0.0));
  CHECK(world.is_alive(barrier_entity));
  barrier.step(world);
  CHECK(world.is_alive(barrier_entity));
  CHECK(world.has<arksim::Destroyed>(barrier_entity));
  arksim::cleanup_destroyed(world);
  CHECK(!world.is_alive(barrier_entity));

  arksim::Entity shield_entity = arksim::make_shield(world, target, 1);
  auto& shield = world.get<arksim::Shield>(shield_entity);
  bool blocked = false;
  shield.OnShieldBlock.add(1, 0, [&](arksim::World&, arksim::Entity obj, arksim::Damage dmg) {
    blocked = true;
    CHECK(obj.entity_id == target.entity_id);
    CHECK(dmg.amount == doctest::Approx(7.0));
  });
  shield.UseShield(world, arksim::Damage{7.0, arksim::Damage::physical});
  CHECK(blocked);
  CHECK(shield.hp == 0);
  CHECK(world.is_alive(shield_entity));
  shield.step(world, 1);
  CHECK(world.is_alive(shield_entity));
  CHECK(world.has<arksim::Destroyed>(shield_entity));
  arksim::cleanup_destroyed(world);
  CHECK(!world.is_alive(shield_entity));
}

TEST_CASE("vec basic ops") {
  using arksim::dot;
  using arksim::rotate;
  using arksim::vec;

  vec<arksim::f32> v{3.0f, 4.0f};
  CHECK(v.length() == doctest::Approx(5.0f));

  auto n = v.normalized();
  CHECK(n.length() == doctest::Approx(1.0f));
  CHECK(dot(n, n) == doctest::Approx(1.0f));

  const vec<arksim::f32> x{1.0f, 0.0f};
  const vec<arksim::f32> y{0.0f, 1.0f};
  CHECK(arksim::cross(x, y) == doctest::Approx(1.0f));

  const arksim::f32 half_pi = static_cast<arksim::f32>(3.14159265358979323846 / 2.0);
  const auto r = rotate(x, half_pi);
  CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(r.y == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("Position homing uses idx+gen and rotates toward target") {
  arksim::World world;
  const arksim::Tick one_second = arksim::Tick{1} << 30;

  arksim::Entity mover = world.create();
  arksim::Entity target = world.create();

  arksim::Position mover_pos;
  mover_pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  mover_pos.dir = arksim::vec<arksim::f32>{0.0f, 1.0f}; // up
  mover_pos.current_speed = 1.0f;
  mover_pos.target_speed = 1.0f;
  mover_pos.accel_speed = 0.0f;
  mover_pos.homing_angle_accel = static_cast<arksim::f32>(3.14159265358979323846 / 2.0); // turn 90deg in 1s
  mover_pos.set_homing_target(target);

  arksim::Position target_pos;
  target_pos.pos = arksim::vec<arksim::f32>{1.0f, 0.0f};
  world.add<arksim::Position>(mover, mover_pos);
  world.add<arksim::Position>(target, target_pos);

  auto& p = world.get<arksim::Position>(mover);
  p.step(world, one_second);

  CHECK(p.pos.x == doctest::Approx(1.0f).epsilon(1e-5));
  CHECK(p.pos.y == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("Position physical_step updates speed without target_speed clamp") {
  arksim::World world;
  const arksim::Tick one_second = arksim::Tick{1} << 30;

  arksim::Entity e = world.create();
  arksim::Position p;
  p.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  p.dir = arksim::vec<arksim::f32>{1.0f, 0.0f};
  p.current_speed = 1.0f;
  p.target_speed = 0.0f; // should be ignored by physical_step
  p.accel_speed = -2.0f;
  p.set_step_mode(arksim::Position::StepMode::Physical);
  world.add<arksim::Position>(e, p);

  auto& ref = world.get<arksim::Position>(e);
  ref.step(world, one_second);

  CHECK(ref.pos.x == doctest::Approx(1.0f).epsilon(1e-5));
  CHECK(ref.current_speed == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("Position stop_step stops movement") {
  arksim::World world;
  const arksim::Tick one_second = arksim::Tick{1} << 30;

  arksim::Entity e = world.create();
  arksim::Position p;
  p.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  p.dir = arksim::vec<arksim::f32>{1.0f, 0.0f};
  p.current_speed = 5.0f;
  p.set_step_mode(arksim::Position::StepMode::Stop);
  world.add<arksim::Position>(e, p);

  auto& ref = world.get<arksim::Position>(e);
  ref.step(world, one_second);

  CHECK(ref.pos.x == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(ref.pos.y == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(ref.current_speed == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("Map bankers rounding") {
  CHECK(arksim::Map::round_half_to_even(0.49) == 0);
  CHECK(arksim::Map::round_half_to_even(0.50) == 0);
  CHECK(arksim::Map::round_half_to_even(1.50) == 2);
  CHECK(arksim::Map::round_half_to_even(2.50) == 2);
  CHECK(arksim::Map::round_half_to_even(-0.50) == 0);
  CHECK(arksim::Map::round_half_to_even(-1.50) == -2);
  CHECK(arksim::Map::round_half_to_even(-2.50) == -2);

  CHECK(arksim::Map::tile_at(arksim::vec<arksim::f32>{0.5f, 0.5f}) == arksim::TileCoord{0, 0});
  CHECK(arksim::Map::tile_at(arksim::vec<arksim::f32>{1.5f, 0.0f}) == arksim::TileCoord{2, 0});
  CHECK(arksim::Map::tile_at(arksim::vec<arksim::f32>{2.5f, 0.0f}) == arksim::TileCoord{2, 0});
}

TEST_CASE("Map flags, penalties, version") {
  arksim::Map map(2, 2);
  const auto v0 = map.version();

  map.set_flag(arksim::TileCoord{0, 0}, arksim::TileFlags::Obstacle, true);
  CHECK(map.version() == v0 + 1);
  CHECK(map.obstacle_penalty(arksim::TileCoord{0, 0}, arksim::MoveMode::Ground) == 1000);
  CHECK(map.passable(arksim::TileCoord{0, 0}, arksim::MoveMode::Ground));

  const auto v1 = map.version();
  map.set_flag(arksim::TileCoord{0, 0}, arksim::TileFlags::Obstacle, true);
  CHECK(map.version() == v1); // no change

  map.set_flag(arksim::TileCoord{0, 0}, arksim::TileFlags::Hole, true);
  CHECK(map.is_hole(arksim::TileCoord{0, 0}));
  CHECK(map.obstacle_penalty(arksim::TileCoord{0, 0}, arksim::MoveMode::Ground) == 1'000'000);

  map.set_flag(arksim::TileCoord{1, 1}, arksim::TileFlags::Unpassable, true);
  CHECK(!map.passable(arksim::TileCoord{1, 1}, arksim::MoveMode::Ground));
}

TEST_CASE("BresenhamCache thick line basic") {
  arksim::BresenhamCache cache;

  const auto& horiz = cache.thick_line_offsets(3, 0);
  CHECK(std::find(horiz.begin(), horiz.end(), arksim::TileCoord{0, 0}) != horiz.end());
  CHECK(std::find(horiz.begin(), horiz.end(), arksim::TileCoord{1, 0}) != horiz.end());
  CHECK(std::find(horiz.begin(), horiz.end(), arksim::TileCoord{2, 0}) != horiz.end());
  CHECK(std::find(horiz.begin(), horiz.end(), arksim::TileCoord{3, 0}) != horiz.end());

  const auto& vert = cache.thick_line_offsets(0, 3);
  CHECK(std::find(vert.begin(), vert.end(), arksim::TileCoord{0, 0}) != vert.end());
  CHECK(std::find(vert.begin(), vert.end(), arksim::TileCoord{0, 1}) != vert.end());
  CHECK(std::find(vert.begin(), vert.end(), arksim::TileCoord{0, 2}) != vert.end());
  CHECK(std::find(vert.begin(), vert.end(), arksim::TileCoord{0, 3}) != vert.end());
}

TEST_CASE("PathMapCache build + mapVersion invalidation") {
  arksim::Map map(3, 3);
  arksim::BresenhamCache bres;
  arksim::PathMapCache cache(map, bres);

  const arksim::TileCoord target{2, 2};
  const arksim::PathMap& pm0 = cache.get_ground(target, false);
  CHECK(pm0.dist(arksim::TileCoord{0, 0}) == 4);

  // Invalidate by changing map.
  map.set_flag(arksim::TileCoord{0, 1}, arksim::TileFlags::Unpassable, true);
  map.set_flag(arksim::TileCoord{1, 0}, arksim::TileFlags::Unpassable, true);

  const arksim::PathMap& pm1 = cache.get_ground(target, false);
  CHECK(pm1.dist(arksim::TileCoord{0, 0}) == arksim::PathMap::kInf);

  // Air mode: no PathMap needed.
  CHECK(cache.try_get(arksim::MoveMode::Air, target, false) == nullptr);
}

TEST_CASE("SpatialGrid tile query and circle query") {
  arksim::World world;

  arksim::Entity e1 = world.create();
  arksim::Position p1;
  p1.pos = arksim::vec<arksim::f32>{1.2f, 1.0f};
  arksim::Area a1;
  a1.type = arksim::Area::Type::Circle;
  a1.radius = arksim::vec<arksim::f32>{0.6f, 0.0f};
  arksim::Spatial s1;
  s1.flags = arksim::TypeFlags::Enemy;
  world.add<arksim::Position>(e1, p1);
  world.add<arksim::Area>(e1, a1);
  world.add<arksim::Spatial>(e1, s1);

  arksim::Entity e2 = world.create();
  arksim::Position p2;
  p2.pos = arksim::vec<arksim::f32>{3.0f, 3.0f};
  arksim::Area a2;
  a2.type = arksim::Area::Type::Circle;
  a2.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
  arksim::Spatial s2;
  s2.flags = arksim::TypeFlags::Ally;
  world.add<arksim::Position>(e2, p2);
  world.add<arksim::Area>(e2, a2);
  world.add<arksim::Spatial>(e2, s2);

  arksim::SpatialIndex index;
  index.reset(5, 5);
  index.rebuild(world);

  std::vector<arksim::Entity> hits;

  const arksim::TileCoord tiles[] = {arksim::TileCoord{1, 1}, arksim::TileCoord{2, 1}};
  index.occupation.query_tiles(tiles, arksim::TypeFlags::Enemy,
                               [&](const arksim::SpatialEntry& e) { hits.push_back(e.entity); });
  CHECK(hits.size() == 1);
  CHECK(hits[0].entity_idx == e1.entity_idx);
  CHECK(hits[0].gen == e1.gen);

  hits.clear();
  index.center.query_circle(arksim::vec<arksim::f32>{1.2f, 1.0f}, 0.2f, arksim::TypeFlags::Enemy,
                            [&](const arksim::SpatialEntry& e) { hits.push_back(e.entity); });
  CHECK(hits.size() == 1);
  CHECK(hits[0].entity_idx == e1.entity_idx);

  std::vector<arksim::SpatialEntry> collected;
  index.center.collect_circle(arksim::vec<arksim::f32>{1.2f, 1.0f}, 0.2f, arksim::TypeFlags::Enemy, collected);
  CHECK(collected.size() == 1);
  CHECK(collected[0].entity.entity_idx == e1.entity_idx);

  hits.clear();
  index.center.query_circle(arksim::vec<arksim::f32>{1.2f, 1.0f}, 0.2f, arksim::TypeFlags::Ally,
                            [&](const arksim::SpatialEntry& e) { hits.push_back(e.entity); });
  CHECK(hits.empty());

  index.occupation.collect_tiles(tiles, arksim::TypeFlags::Enemy, collected);
  CHECK(collected.size() == 1);
  CHECK(collected[0].entity.entity_idx == e1.entity_idx);
}

TEST_CASE("TargetSelector primary sort + secondary swap") {
  arksim::World world;

  auto make_target = [&](arksim::TypeFlags flags, arksim::vec<arksim::f32> pos) {
    arksim::Entity e = world.create();
    arksim::Position p;
    p.pos = pos;
    arksim::Area a;
    a.type = arksim::Area::Type::Circle;
    a.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
    arksim::Spatial s;
    s.flags = flags;
    world.add<arksim::Position>(e, p);
    world.add<arksim::Area>(e, a);
    world.add<arksim::Spatial>(e, s);
    return e;
  };

  const arksim::Entity e1 = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Ground, {1.0f, 1.0f});
  const arksim::Entity e2 = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Air, {2.0f, 1.0f});
  const arksim::Entity e3 = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Ground | arksim::TypeFlags::Blocked,
                                        {3.0f, 1.0f});
  const arksim::Entity e4 = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Air | arksim::TypeFlags::Camouflage,
                                        {4.0f, 1.0f});

  arksim::SpatialIndex spatial;
  spatial.reset(10, 10);
  spatial.rebuild(world);

  arksim::TargetSelector sel;
  sel.range.kind = arksim::TargetRange::Kind::Circle;
  sel.range.grid = arksim::TargetRange::Grid::Occupation;
  sel.range.required = arksim::TypeFlags::Enemy;
  sel.range.center = arksim::vec<arksim::f32>{2.5f, 1.0f};
  sel.range.radius = 10.0f;

  arksim::TargetSelectorScratch scratch;

  // CreatedTimeDesc => last-created first.
  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeDesc;
  sel.arranger.secondary = arksim::TargetArranger::Secondary::None;
  sel.arranger.max_targets = 0;
  auto out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 4);
  CHECK(out[0].entity_id == e4.entity_id);
  CHECK(out[1].entity_id == e3.entity_id);
  CHECK(out[2].entity_id == e2.entity_id);
  CHECK(out[3].entity_id == e1.entity_id);

  // FlyFirst secondary filter: swap with earliest non-air (wiki semantics).
  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeAsc;
  sel.arranger.prefer_blocked = false;
  sel.arranger.secondary = arksim::TargetArranger::Secondary::FlyFirst;
  sel.arranger.max_targets = 3;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 3);
  CHECK(out[0].entity_id == e2.entity_id);
  CHECK(out[1].entity_id == e4.entity_id);
  CHECK(out[2].entity_id == e3.entity_id);

  // prefer_blocked: blocked entries are swapped to the front after primary ordering.
  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeAsc;
  sel.arranger.prefer_blocked = true;
  sel.arranger.secondary = arksim::TargetArranger::Secondary::None;
  sel.arranger.max_targets = 0;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 4);
  CHECK(out[0].entity_id == e3.entity_id);

  // excluded flags: remove candidates before sorting.
  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeDesc;
  sel.arranger.prefer_blocked = false;
  sel.arranger.secondary = arksim::TargetArranger::Secondary::None;
  sel.arranger.excluded = arksim::TypeFlags::Camouflage;
  sel.arranger.max_targets = 0;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 3);
  CHECK(out[0].entity_id == e3.entity_id);
  CHECK(out[1].entity_id == e2.entity_id);
  CHECK(out[2].entity_id == e1.entity_id);
}

TEST_CASE("TargetSelector stress: unique + deterministic") {
  arksim::World world;

  constexpr int kW = 32;
  constexpr int kH = 32;
  constexpr int kN = 500;

  std::vector<arksim::Entity> created;
  created.reserve(kN);

  for (int i = 0; i < kN; ++i) {
    arksim::Entity e = world.create();
    created.push_back(e);

    arksim::Position p;
    p.pos = arksim::vec<arksim::f32>{static_cast<arksim::f32>(i % kW) + 0.1f, static_cast<arksim::f32>(i / kW) + 0.1f};
    world.add<arksim::Position>(e, p);

    arksim::Area a;
    a.type = arksim::Area::Type::Circle;
    a.radius = arksim::vec<arksim::f32>{0.9f, 0.0f}; // overlaps multiple tiles
    world.add<arksim::Area>(e, a);

    arksim::Spatial s;
    s.flags = arksim::TypeFlags::Enemy | ((i % 10 == 0) ? arksim::TypeFlags::Air : arksim::TypeFlags::Ground);
    world.add<arksim::Spatial>(e, s);
  }

  arksim::SpatialIndex spatial;
  spatial.reset(kW, kH);
  spatial.rebuild(world);

  arksim::TargetSelector sel;
  sel.range.kind = arksim::TargetRange::Kind::Circle;
  sel.range.grid = arksim::TargetRange::Grid::Occupation;
  sel.range.required = arksim::TypeFlags::Enemy;
  sel.range.center = arksim::vec<arksim::f32>{16.0f, 16.0f};
  sel.range.radius = 100.0f;

  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeAsc;
  sel.arranger.prefer_blocked = false;
  sel.arranger.secondary = arksim::TargetArranger::Secondary::None;
  sel.arranger.excluded = arksim::TypeFlags::None;
  sel.arranger.max_targets = 0;

  arksim::TargetSelectorScratch scratch;
  auto out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == static_cast<std::size_t>(kN));

  std::vector<std::uint64_t> ids;
  ids.reserve(out.size());
  for (const arksim::Entity e : out) {
    ids.push_back(e.entity_id);
  }
  CHECK(std::is_sorted(ids.begin(), ids.end()));
  auto it = std::unique(ids.begin(), ids.end());
  CHECK(it == ids.end());
}

TEST_CASE("TargetSelector selectability (hostile/camouflage/unselectable/heal/destroyed)") {
  arksim::World world;

  auto make_target = [&](arksim::TypeFlags flags, arksim::vec<arksim::f32> pos) {
    arksim::Entity e = world.create();
    arksim::Position p;
    p.pos = pos;
    arksim::Area a;
    a.type = arksim::Area::Type::Circle;
    a.radius = arksim::vec<arksim::f32>{0.1f, 0.0f};
    arksim::Spatial s;
    s.flags = flags;
    world.add<arksim::Position>(e, p);
    world.add<arksim::Area>(e, a);
    world.add<arksim::Spatial>(e, s);
    return e;
  };

  const arksim::Entity normal = make_target(arksim::TypeFlags::Enemy, {1.0f, 1.0f});
  const arksim::Entity camo = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Camouflage, {2.0f, 1.0f});
  const arksim::Entity invincible = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::Invincible, {3.0f, 1.0f});
  const arksim::Entity nonheal = make_target(arksim::TypeFlags::Enemy | arksim::TypeFlags::NonHeal, {4.0f, 1.0f});

  arksim::SpatialIndex spatial;
  spatial.reset(10, 10);
  spatial.rebuild(world);

  // Simulate "destroyed this frame": it stays in spatial grid, but should be filtered by selector.
  world.add<arksim::Destroyed>(normal);

  arksim::TargetSelector sel;
  sel.range.kind = arksim::TargetRange::Kind::Circle;
  sel.range.grid = arksim::TargetRange::Grid::Occupation;
  sel.range.required = arksim::TypeFlags::Enemy;
  sel.range.center = arksim::vec<arksim::f32>{2.5f, 1.0f};
  sel.range.radius = 10.0f;

  sel.arranger.primary = arksim::TargetArranger::Primary::CreatedTimeAsc;
  sel.arranger.max_targets = 0;

  arksim::TargetSelectorScratch scratch;

  // Default: only excludes Destroyed (exclude_destroyed defaults to true).
  auto out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 3);
  CHECK(out[0].entity_id == camo.entity_id);
  CHECK(out[1].entity_id == invincible.entity_id);
  CHECK(out[2].entity_id == nonheal.entity_id);

  // Hostile rules: camouflage + invincible filtered by default.
  sel.arranger.relationship = arksim::TargetArranger::Relationship::Hostile;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 1);
  CHECK(out[0].entity_id == nonheal.entity_id);

  sel.arranger.ignore_camouflage = true;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 2);
  CHECK(out[0].entity_id == camo.entity_id);
  CHECK(out[1].entity_id == nonheal.entity_id);

  sel.arranger.ignore_unselectable = true;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 3);
  CHECK(out[0].entity_id == camo.entity_id);
  CHECK(out[1].entity_id == invincible.entity_id);
  CHECK(out[2].entity_id == nonheal.entity_id);

  sel.arranger.is_heal = true;
  out = sel.select(world, spatial, scratch);
  REQUIRE(out.size() == 2);
  CHECK(out[0].entity_id == camo.entity_id);
  CHECK(out[1].entity_id == invincible.entity_id);
}

TEST_CASE("RouteMove ground follows PathMap toward target") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 2.0;
  rm.move_multiplier = 0.5f; // theoretical = 1.0 unit/s
  rm.steering_factor = 100.0f;
  rm.max_steering_force = 1000.0f;
  rm.set_target(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  for (int i = 0; i < 4; ++i) {
    rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  }

  const auto& p = world.get<arksim::Position>(e);
  CHECK(p.pos.x == doctest::Approx(4.0f).epsilon(1e-5));
  CHECK(p.pos.y == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("RouteMove route runtime: wait then move") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 2.0;
  rm.move_multiplier = 0.5f; // theoretical = 1.0 unit/s
  rm.steering_factor = 100.0f;
  rm.max_steering_force = 1000.0f;
  rm.push_wait_seconds(2.0);
  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});
  rm.set_end(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  // Wait for 2 seconds.
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 1);
  CHECK(world.get<arksim::Position>(e).pos.x == doctest::Approx(0.0f));

  // Then move 4 seconds at 1.0 unit/s.
  for (int i = 0; i < 4; ++i) {
    rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  }

  const auto& p = world.get<arksim::Position>(e);
  CHECK(p.pos.x == doctest::Approx(4.0f).epsilon(1e-5));
  CHECK(p.pos.y == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(rm.reached_end);
}

TEST_CASE("RouteMove checkpoint: WAIT_FOR_PLAY_TIME") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 1.0;
  rm.move_multiplier = 0.0f; // freeze movement for this test
  rm.push_wait_play_time(2.0);
  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 0);

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 1);
}

TEST_CASE("RouteMove checkpoint: WAIT_CURRENT_FRAGMENT_TIME") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 1.0;
  rm.move_multiplier = 0.0f; // freeze movement for this test
  rm.push_wait_fragment_time(2.0);
  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 0);

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 1);
}

TEST_CASE("RouteMove checkpoint: WAIT_CURRENT_WAVE_TIME") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 1.0;
  rm.move_multiplier = 0.0f; // freeze movement for this test
  rm.push_wait_wave_time(2.0);
  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 0);

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 1);
}

TEST_CASE("RouteMove checkpoint: WAIT_BOSSRUSH_WAVE") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 1.0;
  rm.move_multiplier = 0.0f; // freeze movement for this test
  rm.region_index = 5;
  rm.push_wait_bossrush_wave(3);
  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;

  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 0);

  rm.region_index = 8;
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);
  CHECK(rm.cp_index == 1);
}

TEST_CASE("RouteMove checkpoint: PATROL_MOVE loops to first") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 1.0;
  rm.move_multiplier = 0.0f; // freeze movement for this test

  rm.push_move_cp(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});
  rm.push_patrol_move_cp(arksim::TileCoord{0, 0}, arksim::vec<arksim::f32>{0.0f, 0.0f});

  rm.cp_index = 1; // start at the last PATROL_MOVE checkpoint
  rm.entered_cp_index = static_cast<std::uint32_t>(-1);

  const arksim::Tick one_second = arksim::Tick{1} << 30;
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);

  CHECK(rm.cp_index == 0);
}

TEST_CASE("RouteMove active gate disables movement") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.active = false;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 2.0;
  rm.move_multiplier = 0.5f;
  rm.steering_factor = 100.0f;
  rm.max_steering_force = 1000.0f;
  rm.set_target(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);

  const auto& p = world.get<arksim::Position>(e);
  CHECK(p.pos.x == doctest::Approx(0.0f));
  CHECK(p.pos.y == doctest::Approx(0.0f));
}

TEST_CASE("RouteMove does not run when Unbalance active") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(e, pos);

  arksim::Unbalance ub;
  ub.active = true;
  world.add<arksim::Unbalance>(e, ub);

  arksim::Map map(5, 5);
  arksim::BresenhamCache bres;
  arksim::PathMapCache pm_cache(map, bres);

  arksim::RouteMove rm;
  rm.active = true;
  rm.mode = arksim::MoveMode::Ground;
  rm.allow_diagonal_move = false;
  rm.cursor_offset = arksim::vec<arksim::f32>{0.0f, 0.0f};
  rm.move_speed.base = 2.0;
  rm.move_multiplier = 0.5f;
  rm.steering_factor = 100.0f;
  rm.max_steering_force = 1000.0f;
  rm.set_target(arksim::TileCoord{4, 0}, arksim::vec<arksim::f32>{4.0f, 0.0f});

  const arksim::Tick one_second = arksim::Tick{1} << 30;
  rm.step(world, e, world.get<arksim::Position>(e), map, pm_cache, one_second);

  const auto& p = world.get<arksim::Position>(e);
  CHECK(p.pos.x == doctest::Approx(0.0f));
  CHECK(p.pos.y == doctest::Approx(0.0f));
}

TEST_CASE("Unbalance uses Position velocity for movement") {
  arksim::World world;
  arksim::Entity e = world.create();

  arksim::Position pos;
  pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  pos.dir = arksim::vec<arksim::f32>{1.0f, 0.0f};
  pos.current_speed = 1.0f;
  world.add<arksim::Position>(e, pos);

  arksim::Map map(10, 10);

  arksim::Unbalance ub;
  ub.active = true;
  ub.friction_force = 0.0f;
  ub.avoid_strength = 0.0f;

  const arksim::Tick one_second = arksim::Tick{1} << 30;
  ub.step(world, e, world.get<arksim::Position>(e), map, one_second);

  const auto& p = world.get<arksim::Position>(e);
  CHECK(p.pos.x == doctest::Approx(1.0f).epsilon(1e-5));
  CHECK(p.pos.y == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(p.current_speed == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("Blocker engages nearest enemy and forced-moves to stable pos") {
  arksim::World world;
  arksim::SimContext ctx;
  ctx.reset_map(10, 10);

  const arksim::Tick kTicksPerSecond = (arksim::Tick{1} << 30);
  const arksim::Tick dt = (kTicksPerSecond + 9) / 10; // ceil(0.1s)

  arksim::Entity blocker = world.create();
  arksim::Position blocker_pos;
  blocker_pos.pos = arksim::vec<arksim::f32>{0.0f, 0.0f};
  world.add<arksim::Position>(blocker, blocker_pos);
  arksim::Blocker blk;
  blk.block_radius = 1.0f;
  blk.block_capacity = 1;
  blk.scan_interval = 0; // scan every tick for this test
  world.add<arksim::Blocker>(blocker, blk);

  auto make_enemy = [&](arksim::f32 x) -> arksim::Entity {
    arksim::Entity enemy = world.create();
    arksim::Position pos;
    pos.pos = arksim::vec<arksim::f32>{x, 0.0f};
    world.add<arksim::Position>(enemy, pos);
    world.add<arksim::Area>(enemy, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
    world.add<arksim::Spatial>(enemy, arksim::Spatial{arksim::TypeFlags::Enemy | arksim::TypeFlags::Blockable});
    world.add<arksim::RouteMove>(enemy, arksim::RouteMove{});
    return enemy;
  };

  arksim::Entity e1 = make_enemy(0.1f);
  arksim::Entity e2 = make_enemy(0.2f);

  ctx.spatial.rebuild(world);

  auto& blocker_comp = world.get<arksim::Blocker>(blocker);
  blocker_comp.step(world, blocker, ctx.spatial, dt);

  CHECK(blocker_comp.blocked.size() == 1);
  CHECK(blocker_comp.blocked[0].entity_id == e1.entity_id);

  auto& rm1 = world.get<arksim::RouteMove>(e1);
  CHECK(rm1.is_blocked());
  CHECK(rm1.blocked_by.entity_id == blocker.entity_id);
  CHECK(rm1.is_bound);
  CHECK(arksim::has_flags(world.get<arksim::Spatial>(e1).flags, arksim::TypeFlags::Blocked));

  // Stable position should be pushed to MIN_SEPARATION=0.5 on +X.
  CHECK(rm1.stable_block_pos.x == doctest::Approx(0.5f).epsilon(1e-6));
  CHECK(rm1.stable_block_pos.y == doctest::Approx(0.0f).epsilon(1e-6));

  // Forced move runs over 0.2s, so with dt=0.1s: halfway after 1 step, done after 2.
  blocker_comp.step(world, blocker, ctx.spatial, dt);
  CHECK(world.get<arksim::Position>(e1).pos.x == doctest::Approx(0.3f).epsilon(1e-4));

  blocker_comp.step(world, blocker, ctx.spatial, dt);
  CHECK(world.get<arksim::Position>(e1).pos.x == doctest::Approx(0.5f).epsilon(1e-4));
}

TEST_CASE("Blocker releases enemy when out of range") {
  arksim::World world;
  arksim::SimContext ctx;
  ctx.reset_map(10, 10);

  const arksim::Tick kTicksPerSecond = (arksim::Tick{1} << 30);
  const arksim::Tick dt = (kTicksPerSecond + 9) / 10; // ceil(0.1s)

  arksim::Entity blocker = world.create();
  world.add<arksim::Position>(blocker, arksim::Position{arksim::vec<arksim::f32>{0.0f, 0.0f}});
  arksim::Blocker blk;
  blk.block_radius = 1.0f;
  blk.block_capacity = 1;
  blk.scan_interval = 0;
  world.add<arksim::Blocker>(blocker, blk);

  arksim::Entity enemy = world.create();
  world.add<arksim::Position>(enemy, arksim::Position{arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Area>(enemy, arksim::Area{arksim::Area::Type::Circle, arksim::vec<arksim::f32>{0.1f, 0.0f}});
  world.add<arksim::Spatial>(enemy, arksim::Spatial{arksim::TypeFlags::Enemy | arksim::TypeFlags::Blockable});
  world.add<arksim::RouteMove>(enemy, arksim::RouteMove{});

  ctx.spatial.rebuild(world);

  auto& blocker_comp = world.get<arksim::Blocker>(blocker);
  blocker_comp.step(world, blocker, ctx.spatial, dt);
  CHECK(world.get<arksim::RouteMove>(enemy).is_blocked());

  // Teleport enemy out of range and run blocker maintenance.
  world.get<arksim::Position>(enemy).pos = arksim::vec<arksim::f32>{10.0f, 0.0f};
  blocker_comp.step(world, blocker, ctx.spatial, dt);

  CHECK(!world.get<arksim::RouteMove>(enemy).is_blocked());
  CHECK(!world.get<arksim::RouteMove>(enemy).is_bound);
  CHECK(!arksim::has_flags(world.get<arksim::Spatial>(enemy).flags, arksim::TypeFlags::Blocked));
  CHECK(blocker_comp.blocked.empty());
}

TEST_CASE("Hole kills non-flying after movement stage") {
  arksim::SimState sim(1);
  arksim::SimContext ctx;
  ctx.reset_map(1, 1);
  ctx.map.set_flag(arksim::TileCoord{0, 0}, arksim::TileFlags::Hole, true);

  arksim::World& world = sim.world();

  // Ground unit: should be marked Destroyed at end of frame, then removed next frame.
  arksim::Entity ground = world.create();
  world.add<arksim::Position>(ground, arksim::Position{arksim::vec<arksim::f32>{0.0f, 0.0f}});
  arksim::RouteMove rm_ground;
  rm_ground.mode = arksim::MoveMode::Ground;
  world.add<arksim::RouteMove>(ground, rm_ground);

  sim.step_frame(ctx);
  CHECK(world.has<arksim::Destroyed>(ground));

  sim.step_frame(ctx);
  CHECK(!world.is_alive(ground));

  // Flying unit: should not be killed by hole.
  arksim::Entity flying = world.create();
  world.add<arksim::Position>(flying, arksim::Position{arksim::vec<arksim::f32>{0.0f, 0.0f}});
  arksim::RouteMove rm_fly;
  rm_fly.mode = arksim::MoveMode::Air;
  world.add<arksim::RouteMove>(flying, rm_fly);

  sim.step_frame(ctx);
  CHECK(world.is_alive(flying));
  CHECK(!world.has<arksim::Destroyed>(flying));
}

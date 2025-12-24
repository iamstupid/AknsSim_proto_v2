#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "sim_core/effect.hpp"
#include "sim_core/entity_component.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/components/position.hpp"
#include "sim_core/rng.hpp"
#include "sim_core/sim_state.hpp"
#include "sim_core/vec.hpp"

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

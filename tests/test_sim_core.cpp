#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "sim_core/effect.hpp"
#include "sim_core/entity_component.hpp"
#include "sim_core/components/buff.hpp"
#include "sim_core/components/barrier_shield.hpp"
#include "sim_core/components/hp.hpp"
#include "sim_core/rng.hpp"
#include "sim_core/sim_state.hpp"

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

TEST_CASE("Effect ordering") {
  arksim::EffectQueue queue;

  arksim::Effect a;
  a.priority = 1;
  a.dst = arksim::UnitId{2};
  a.src = arksim::UnitId{1};
  a.seq = 2;

  arksim::Effect b;
  b.priority = 0;
  b.dst = arksim::UnitId{3};
  b.src = arksim::UnitId{1};
  b.seq = 1;

  arksim::Effect c;
  c.priority = 1;
  c.dst = arksim::UnitId{2};
  c.src = arksim::UnitId{1};
  c.seq = 1;

  queue.push(a);
  queue.push(b);
  queue.push(c);

  const auto sorted = queue.drain_sorted();
  REQUIRE(sorted.size() == 3);
  CHECK(sorted[0].priority == 0);
  CHECK(sorted[1].seq == 1);
  CHECK(sorted[2].seq == 2);
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
  flecs::world world;
  flecs::entity e = world.entity();

  struct DemoComponent : arksim::EntityComponent {
    int value = 0;
  } comp;

  comp.bind(world, e);

  bool intercepted = false;
  comp.OnDestroy.add(1, 0, [&](flecs::world&, flecs::entity, arksim::EntityComponent& c) {
    if (!intercepted) {
      c.destroyed = false;
      intercepted = true;
    }
  });

  arksim::Destroy(comp);
  CHECK(e.is_alive());

  arksim::Destroy(comp);
  CHECK(!e.is_alive());
}

TEST_CASE("HP damage/heal triggers") {
  flecs::world world;
  flecs::entity target = world.entity();
  flecs::entity source = world.entity();

  arksim::HP hp;
  hp.total_hp = arksim::BuffNum(100.0);
  hp.ratio = 1.0;

  bool underflow_called = false;
  bool overflow_called = false;

  hp.OnUnderflow.add(1, 0, [&](flecs::entity, flecs::entity, double amount) {
    underflow_called = true;
    CHECK(amount == doctest::Approx(10.0));
  });

  hp.OnOverflow.add(2, 0, [&](flecs::entity, flecs::entity, double amount) {
    overflow_called = true;
    CHECK(amount == doctest::Approx(15.0));
  });

  target.set<arksim::HP>(hp);

  arksim::do_damage(target, 110.0, source);
  CHECK(underflow_called);

  arksim::do_heal(target, 125.0, source);
  CHECK(overflow_called);
}

TEST_CASE("Buff life countdown") {
  flecs::world world;
  flecs::entity buff_entity = world.entity();
  arksim::Buff buff;
  buff.life_remain = 10;
  buff.step(buff_entity, 4);
  CHECK(buff_entity.is_alive());
  buff.step(buff_entity, 4);
  CHECK(buff_entity.is_alive());
  buff.step(buff_entity, 4);
  CHECK(!buff_entity.is_alive());
}

TEST_CASE("Barrier and Shield usage") {
  flecs::world world;
  flecs::entity target = world.entity();
  arksim::DefStats stats{arksim::BuffNum(0.0), arksim::BuffNum(0.0), arksim::BuffNum(0.0)};
  target.set<arksim::DefStats>(stats);

  flecs::entity barrier_entity = arksim::make_barrier(target, 5.0, 0.0);
  auto& barrier = barrier_entity.get_mut<arksim::Barrier>();
  barrier.UseBarrier(3.0);
  CHECK(barrier.amount == doctest::Approx(2.0));
  CHECK(barrier_entity.is_alive());
  barrier.UseBarrier(3.0);
  CHECK(barrier.amount == doctest::Approx(0.0));
  CHECK(barrier_entity.is_alive());
  barrier.step();
  CHECK(!barrier_entity.is_alive());

  flecs::entity shield_entity = arksim::make_shield(target, 1);
  auto& shield = shield_entity.get_mut<arksim::Shield>();
  bool blocked = false;
  shield.OnShieldBlock.add(1, 0, [&](flecs::entity obj, arksim::Damage dmg) {
    blocked = true;
    CHECK(obj == target);
    CHECK(dmg.amount == doctest::Approx(7.0));
  });
  shield.UseShield(arksim::Damage{7.0, arksim::Damage::physical});
  CHECK(blocked);
  CHECK(shield.hp == 0);
  CHECK(shield_entity.is_alive());
  shield.step(1);
  CHECK(!shield_entity.is_alive());
}

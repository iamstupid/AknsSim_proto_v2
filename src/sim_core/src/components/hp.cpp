#include "sim_core/components/hp.hpp"

namespace arksim {

void do_damage(World& world, Entity object, double amount, Entity source) {
  auto* hp = world.try_get<HP>(object);
  if (!hp) {
    return;
  }

  const double total = hp->total_hp.value();
  if (total <= 0.0) {
    hp->ratio = 0.0;
    return;
  }

  const double current = hp->ratio * total;
  const double next = current - amount;
  hp->ratio = next / total;

  if (current > 0 && next <= 0.0) {
    const double underflow = -next;
    hp->OnUnderflow(world, object, source, underflow);
  }
}

void do_heal(World& world, Entity object, double amount, Entity source) {
  auto* hp = world.try_get<HP>(object);
  if (!hp) {
    return;
  }

  const double total = hp->total_hp.value();
  if (total <= 0.0) {
    return;
  }

  const double current = hp->ratio * total;
  const double next = current + amount;
  hp->ratio = next / total;

  if (next >= total) {
    const double overflow = next - total;
    hp->OnOverflow(world, object, source, overflow);
  }
}

} // namespace arksim

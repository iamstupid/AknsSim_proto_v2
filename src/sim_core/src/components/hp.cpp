#include "sim_core/components/hp.hpp"

namespace arksim {

void do_damage(flecs::entity object, double amount, flecs::entity source) {
  auto* hp = object.try_get_mut<HP>();
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

  if (next < 0.0) {
    const double underflow = -next;
    hp->OnUnderflow(object, source, underflow);
  }

  object.modified<HP>();
}

void do_heal(flecs::entity object, double amount, flecs::entity source) {
  auto* hp = object.try_get_mut<HP>();
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
    hp->OnOverflow(object, source, overflow);
  }

  object.modified<HP>();
}

} // namespace arksim

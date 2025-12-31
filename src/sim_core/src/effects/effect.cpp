#include "sim_core/effects/effect.hpp"

namespace arksim {

void EffectQueue::clear() {
  effects_ = std::queue<Effect>();
}

void EffectQueue::push(const Effect& effect) {
  effects_.push(effect);
}

bool EffectQueue::try_pop(Effect& out) {
  if (effects_.empty()) {
    return false;
  }
  out = effects_.front();
  effects_.pop();
  return true;
}

} // namespace arksim

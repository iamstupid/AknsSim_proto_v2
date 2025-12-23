#include "sim_core/effect.hpp"

#include <algorithm>

namespace arksim {

bool EffectLess::operator()(const Effect& a, const Effect& b) const {
  if (a.priority != b.priority) {
    return a.priority < b.priority;
  }
  if (a.dst.value != b.dst.value) {
    return a.dst.value < b.dst.value;
  }
  if (a.src.value != b.src.value) {
    return a.src.value < b.src.value;
  }
  return a.seq < b.seq;
}

void EffectQueue::clear() {
  effects_.clear();
}

void EffectQueue::push(const Effect& effect) {
  effects_.push_back(effect);
}

std::vector<Effect> EffectQueue::drain_sorted() {
  std::vector<Effect> out = std::move(effects_);
  effects_.clear();
  std::stable_sort(out.begin(), out.end(), EffectLess{});
  return out;
}

} // namespace arksim

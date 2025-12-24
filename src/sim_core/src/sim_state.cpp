#include "sim_core/sim_state.hpp"

#include <cmath>
#include <limits>

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
#include <intrin.h>
#endif

#include "sim_core/script_vm.hpp"

namespace arksim {

SimState::SimState(std::uint64_t seed) : rng_(seed) {}

void SimState::set_tick_rate(Tick tick_rate) {
  tick_rate_ = tick_rate;
}

namespace {

std::uint64_t ceil_mul_div_u64(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
  if (c == 0) {
    return 0;
  }

#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(a, b, &high);
  unsigned __int64 rem = 0;
  unsigned __int64 quo = _udiv128(high, low, c, &rem);
  if (rem != 0 && quo != std::numeric_limits<std::uint64_t>::max()) {
    ++quo;
  }
  return quo;
#elif defined(__SIZEOF_INT128__)
  unsigned __int128 prod = static_cast<unsigned __int128>(a) * b;
  unsigned __int128 quo = prod / c;
  unsigned __int128 rem = prod % c;
  if (rem != 0) {
    ++quo;
  }
  if (quo > std::numeric_limits<std::uint64_t>::max()) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(quo);
#else
  const double scaled = static_cast<double>(a) * static_cast<double>(b) / static_cast<double>(c);
  if (scaled >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(scaled == std::floor(scaled) ? scaled : std::floor(scaled) + 1.0);
#endif
}

} // namespace

void SimState::set_tick_rate(std::uint64_t numerator, std::uint64_t denominator) {
  tick_rate_ = ceil_mul_div_u64(numerator, kTicksPerSecond, denominator);
}

float SimState::get_tick_rate_f32() const {
  return static_cast<float>(tick_rate_) / static_cast<float>(kTicksPerSecond);
}

double SimState::get_tick_rate_f64() const {
  return static_cast<double>(tick_rate_) / static_cast<double>(kTicksPerSecond);
}

void SimState::attach_script(ScriptVM* script) {
  script_ = script;
  if (script_) {
    script_->bind_world(this);
  }
}

void SimState::step() {
  resolve();
  tick_ += tick_rate_;
}

void SimState::emit_effect(Effect effect) {
  queue_.push(effect);
}

void SimState::resolve() {
  if (script_) {
    script_->call_on_tick(tick_);
  }
}

std::uint64_t SimState::state_hash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };

  mix(static_cast<std::uint64_t>(tick_));
  mix(static_cast<std::uint64_t>(tick_rate_));
  mix(rng_.state());
  mix(static_cast<std::uint64_t>(queue_.size()));

  return hash;
}

} // namespace arksim

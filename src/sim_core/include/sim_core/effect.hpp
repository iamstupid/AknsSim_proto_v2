#pragma once

#include <array>
#include <cstdint>
#include <queue>
#include <vector>

#include "sim_core/scratch_pad.hpp"
#include "sim_core/types.hpp"

namespace arksim {

class SimState;

struct Effect {
  std::uint32_t type : 8 = 0;
  std::uint32_t fnidx : 24 = 0;
  std::uint32_t src = 0;
  scratch_pad<24> param_buffer{};
};

static_assert(sizeof(Effect) == 32, "Effect must be 32 bytes (cache-friendly fixed size).");

struct EffectHandler {
  using Handler = void (*)(SimState*, const Effect&);

  explicit EffectHandler(SimState* sim = nullptr) : sim_(sim) {
    handlers.fill(nullptr);
  }

  void bind(SimState* sim) { sim_ = sim; }

  void set(std::uint8_t type, Handler handler) {
    handlers[type] = handler;
  }

  void clear(std::uint8_t type) {
    handlers[type] = nullptr;
  }

  void operator()(const Effect& effect) const {
    if (!sim_) {
      return;
    }
    if (auto handler = handlers[effect.type]) {
      handler(sim_, effect);
    }
  }

  std::array<Handler, 256> handlers{};

private:
  SimState* sim_ = nullptr;
};

class EffectQueue {
public:
  void clear();
  void push(const Effect& effect);
  bool try_pop(Effect& out);
  std::size_t size() const { return effects_.size(); }

private:
  std::queue<Effect> effects_;
};

} // namespace arksim

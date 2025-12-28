#pragma once

#include <cstdint>

#include "sim_core/types.hpp"
#include "sim_core/vec.hpp"

namespace arksim {

struct Area {
  enum class Type : std::uint8_t { Circle, Rectangle };

  Type type = Type::Circle;
  vec<f32> radius{}; // circle: x = R; rectangle: x = width, y = height
};

} // namespace arksim


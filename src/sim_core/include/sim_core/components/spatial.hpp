#pragma once

#include <cstdint>

namespace arksim {

enum class TypeFlags : std::uint64_t {
  None = 0,

  Enemy = 1ull << 0,
  Ally = 1ull << 1,
  Neutral = 1ull << 2,

  Normal = 1ull << 3,
  Device = 1ull << 4,
  Obstacle = 1ull << 5,

  Melee = 1ull << 6,
  Ranged = 1ull << 7,

  Flight = 1ull << 8, // "takeoff" mechanism
  Ground = 1ull << 9,
  Air = 1ull << 10,

  Invisible = 1ull << 11,
  Invincible = 1ull << 12,
  Camouflage = 1ull << 13,

  Blockable = 1ull << 14,
  Blocked = 1ull << 15,
  Sleep = 1ull << 16,
  NonHeal = 1ull << 17,
};

constexpr TypeFlags operator|(TypeFlags a, TypeFlags b) {
  return static_cast<TypeFlags>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
constexpr TypeFlags operator&(TypeFlags a, TypeFlags b) {
  return static_cast<TypeFlags>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}
constexpr TypeFlags operator~(TypeFlags a) { return static_cast<TypeFlags>(~static_cast<std::uint64_t>(a)); }

constexpr TypeFlags& operator|=(TypeFlags& a, TypeFlags b) { return a = (a | b); }
constexpr TypeFlags& operator&=(TypeFlags& a, TypeFlags b) { return a = (a & b); }

constexpr bool any(TypeFlags v) { return static_cast<std::uint64_t>(v) != 0; }
constexpr bool has_flags(TypeFlags v, TypeFlags required) { return (v & required) == required; }

struct Spatial {
  TypeFlags flags = TypeFlags::None;
};

} // namespace arksim


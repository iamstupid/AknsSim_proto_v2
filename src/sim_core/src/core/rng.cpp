#include "sim_core/core/rng.hpp"

#include <limits>
#include <utility>

namespace arksim {

// xoshiro256** 1.0
// Written in 2018 by David Blackman and Sebastiano Vigna (vigna@acm.org)
// Public domain (see upstream reference in docs or repository history).

Rng::Rng(std::uint64_t seed) {
  reseed(seed);
}

std::uint64_t Rng::splitmix64(std::uint64_t& x) {
  std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

std::uint64_t Rng::rotl(std::uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

void Rng::reseed(std::uint64_t seed) {
  std::uint64_t x = seed;
  for (int i = 0; i < 4; ++i) {
    s_[i] = splitmix64(x);
  }

  if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) {
    s_[0] = 1;
  }
}

std::uint64_t Rng::next_u64() {
  const std::uint64_t result = rotl(s_[1] * 5ULL, 7) * 9ULL;
  const std::uint64_t t = s_[1] << 17;

  s_[2] ^= s_[0];
  s_[3] ^= s_[1];
  s_[1] ^= s_[2];
  s_[0] ^= s_[3];

  s_[2] ^= t;
  s_[3] = rotl(s_[3], 45);

  return result;
}

std::uint32_t Rng::next_u32() {
  return static_cast<std::uint32_t>(next_u64() >> 32);
}

float Rng::next_f32() {
  const std::uint32_t v = next_u32() >> 8; // 24-bit mantissa
  return static_cast<float>(v) * (1.0f / 16777216.0f);
}

double Rng::next_f64() {
  const std::uint64_t v = next_u64() >> 11; // 53-bit mantissa
  return static_cast<double>(v) * (1.0 / 9007199254740992.0);
}

std::uint32_t Rng::uniform_u32(std::uint32_t min, std::uint32_t max) {
  if (min > max) {
    std::swap(min, max);
  }

  const std::uint32_t range = max - min + 1;
  if (range == 0) {
    return next_u32();
  }

  const std::uint32_t threshold =
      (std::numeric_limits<std::uint32_t>::max() - range + 1u) % range;
  std::uint32_t r = next_u32();
  while (r < threshold) {
    r = next_u32();
  }
  return min + (r % range);
}

std::uint64_t Rng::uniform_u64(std::uint64_t min, std::uint64_t max) {
  if (min > max) {
    std::swap(min, max);
  }

  const std::uint64_t range = max - min + 1;
  if (range == 0) {
    return next_u64();
  }

  const std::uint64_t threshold =
      (std::numeric_limits<std::uint64_t>::max() - range + 1ULL) % range;
  std::uint64_t r = next_u64();
  while (r < threshold) {
    r = next_u64();
  }
  return min + (r % range);
}

std::int32_t Rng::uniform_i32(std::int32_t min, std::int32_t max) {
  const std::uint32_t bias = 0x80000000U;
  std::uint32_t umin = static_cast<std::uint32_t>(min) ^ bias;
  std::uint32_t umax = static_cast<std::uint32_t>(max) ^ bias;
  if (umin > umax) {
    std::swap(umin, umax);
  }
  if (umin == 0 && umax == 0xFFFFFFFFU) {
    return static_cast<std::int32_t>(next_u32());
  }
  const std::uint32_t range = umax - umin + 1;
  const std::uint32_t r = uniform_u32(0, range - 1);
  return static_cast<std::int32_t>((umin + r) ^ bias);
}

std::int64_t Rng::uniform_i64(std::int64_t min, std::int64_t max) {
  const std::uint64_t bias = 0x8000000000000000ULL;
  std::uint64_t umin = static_cast<std::uint64_t>(min) ^ bias;
  std::uint64_t umax = static_cast<std::uint64_t>(max) ^ bias;
  if (umin > umax) {
    std::swap(umin, umax);
  }
  if (umin == 0 && umax == 0xFFFFFFFFFFFFFFFFULL) {
    return static_cast<std::int64_t>(next_u64());
  }
  const std::uint64_t range = umax - umin + 1;
  const std::uint64_t r = uniform_u64(0, range - 1);
  return static_cast<std::int64_t>((umin + r) ^ bias);
}

float Rng::uniform_f32(float min, float max) {
  if (min > max) {
    std::swap(min, max);
  }
  return min + (max - min) * next_f32();
}

double Rng::uniform_f64(double min, double max) {
  if (min > max) {
    std::swap(min, max);
  }
  return min + (max - min) * next_f64();
}

Rng::Snapshot Rng::snapshot() const noexcept {
  Snapshot snap;
  snap.s0 = s_[0];
  snap.s1 = s_[1];
  snap.s2 = s_[2];
  snap.s3 = s_[3];
  return snap;
}

void Rng::restore(const Snapshot& snap) noexcept {
  s_[0] = snap.s0;
  s_[1] = snap.s1;
  s_[2] = snap.s2;
  s_[3] = snap.s3;

  if ((s_[0] | s_[1] | s_[2] | s_[3]) == 0) {
    s_[0] = 1;
  }
}

std::uint64_t Rng::state() const {
  return s_[0] ^ s_[1] ^ s_[2] ^ s_[3];
}

} // namespace arksim

#pragma once

#include <cstdint>

namespace arksim {

class Rng {
public:
  explicit Rng(std::uint64_t seed = 0);

  void reseed(std::uint64_t seed);
  std::uint64_t next_u64();
  std::uint32_t next_u32();
  float next_f32();
  double next_f64();

  std::uint64_t state() const;

  std::uint32_t uniform_u32(std::uint32_t min, std::uint32_t max);
  std::uint64_t uniform_u64(std::uint64_t min, std::uint64_t max);
  std::int32_t uniform_i32(std::int32_t min, std::int32_t max);
  std::int64_t uniform_i64(std::int64_t min, std::int64_t max);
  float uniform_f32(float min, float max);
  double uniform_f64(double min, double max);

private:
  static std::uint64_t splitmix64(std::uint64_t& x);
  static std::uint64_t rotl(std::uint64_t x, int k);

  std::uint64_t s_[4]{};
};

} // namespace arksim

#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>

namespace arksim {

// 2D vector (float-friendly) used in simulation/movement code.
template <typename T>
struct vec {
  T x{};
  T y{};

  constexpr vec() = default;
  constexpr vec(T x_, T y_) : x(x_), y(y_) {}

  constexpr T& operator[](std::size_t i) {
    assert(i < 2);
    return i == 0 ? x : y;
  }
  constexpr const T& operator[](std::size_t i) const {
    assert(i < 2);
    return i == 0 ? x : y;
  }

  constexpr vec operator-() const { return vec{-x, -y}; }

  constexpr vec& operator+=(const vec& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  constexpr vec& operator-=(const vec& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  constexpr vec& operator*=(T s) {
    x *= s;
    y *= s;
    return *this;
  }
  constexpr vec& operator/=(T s) {
    x /= s;
    y /= s;
    return *this;
  }

  friend constexpr vec operator+(vec a, const vec& b) { return a += b; }
  friend constexpr vec operator-(vec a, const vec& b) { return a -= b; }
  friend constexpr vec operator*(vec v, T s) { return v *= s; }
  friend constexpr vec operator*(T s, vec v) { return v *= s; }
  friend constexpr vec operator/(vec v, T s) { return v /= s; }

  constexpr T length_sq() const { return x * x + y * y; }

  T length() const {
    using std::sqrt;
    return static_cast<T>(sqrt(static_cast<double>(length_sq())));
  }

  vec normalized(T eps = static_cast<T>(1e-6)) const {
    const T lsq = length_sq();
    const T eps_sq = eps * eps;
    if (!(lsq > eps_sq)) {
      return vec{};
    }
    using std::sqrt;
    const T inv_len = static_cast<T>(1) / static_cast<T>(sqrt(static_cast<double>(lsq)));
    return vec{x * inv_len, y * inv_len};
  }

  vec& normalize(T eps = static_cast<T>(1e-6)) {
    *this = normalized(eps);
    return *this;
  }
};

template <typename T>
constexpr T dot(const vec<T>& a, const vec<T>& b) {
  return a.x * b.x + a.y * b.y;
}

// 2D "cross product" (z component).
template <typename T>
constexpr T cross(const vec<T>& a, const vec<T>& b) {
  return a.x * b.y - a.y * b.x;
}

template <typename T>
vec<T> rotate(const vec<T>& v, T radians) {
  using std::cos;
  using std::sin;
  const T c = static_cast<T>(cos(static_cast<double>(radians)));
  const T s = static_cast<T>(sin(static_cast<double>(radians)));
  return vec<T>{v.x * c - v.y * s, v.x * s + v.y * c};
}

} // namespace arksim


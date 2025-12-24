#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace arksim {

// A small fixed-size byte buffer that supports:
// - type erasure via raw byte copy
// - typed store/load for trivially-copyable types
// - optional "overlay" access for implicit-lifetime types when alignment allows
template <std::size_t Size>
struct scratch_pad {
  // Keep a fixed alignment so `load<T>()` can return references safely for common types.
  // Note: this means `sizeof(scratch_pad<Size>)` may be rounded up to the alignment.
  alignas(8) std::array<std::uint8_t, Size> data{};

  static constexpr std::size_t size() noexcept { return Size; }

  void clear() noexcept { data.fill(0); }

  std::uint8_t* bytes() noexcept { return data.data(); }
  const std::uint8_t* bytes() const noexcept { return data.data(); }

  void store_bytes(const void* src, std::size_t len, std::size_t offset = 0) {
    assert(src != nullptr || len == 0);
    assert(offset <= Size);
    assert(len <= Size - offset);
    std::memcpy(data.data() + offset, src, len);
  }

  void load_bytes(void* dst, std::size_t len, std::size_t offset = 0) const {
    assert(dst != nullptr || len == 0);
    assert(offset <= Size);
    assert(len <= Size - offset);
    std::memcpy(dst, data.data() + offset, len);
  }

  template <class T>
  void store(const T& src, std::size_t offset = 0) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "scratch_pad::store requires trivially copyable T (use store_bytes for other payloads).");
    assert(offset <= Size);
    assert(sizeof(T) <= Size - offset);
    std::memcpy(data.data() + offset, &src, sizeof(T));
  }

  template <class T>
  void from(const T& src, std::size_t offset = 0) {
    store<T>(src, offset);
  }

  template <class T>
  T read(std::size_t offset = 0) const {
    static_assert(std::is_trivially_copyable_v<T>,
                  "scratch_pad::read requires trivially copyable T (use load_bytes for other payloads).");
    assert(offset <= Size);
    assert(sizeof(T) <= Size - offset);
    T out{};
    std::memcpy(&out, data.data() + offset, sizeof(T));
    return out;
  }

  // Raw typed access into the underlying storage.
  // Preconditions:
  // - alignment must be satisfied (asserted)
  // - the caller must ensure the object lifetime of T has begun in this storage
  template <class T>
  T* ptr(std::size_t offset = 0) noexcept {
    assert(offset <= Size);
    assert(sizeof(T) <= Size - offset);
    const auto addr = reinterpret_cast<std::uintptr_t>(data.data() + offset);
    assert((addr % alignof(T)) == 0 && "ptr<T> alignment not satisfied for this buffer/offset");
    return std::launder(reinterpret_cast<T*>(data.data() + offset));
  }

  template <class T>
  const T* ptr(std::size_t offset = 0) const noexcept {
    return const_cast<scratch_pad*>(this)->ptr<T>(offset);
  }

  // Overlay typed access for implicit-lifetime/trivial payloads.
  template <class T>
  T* overlay(std::size_t offset = 0) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "scratch_pad::overlay requires trivially copyable T.");
    static_assert(std::is_trivially_destructible_v<T>,
                  "scratch_pad::overlay requires trivially destructible T.");
    return ptr<T>(offset);
  }

  template <class T>
  const T* overlay(std::size_t offset = 0) const noexcept {
    return const_cast<scratch_pad*>(this)->overlay<T>(offset);
  }

  // Zero-copy typed access (reference) for implicit-lifetime/trivial payloads.
  template <class T>
  T& load(std::size_t offset = 0) noexcept {
    return *overlay<T>(offset);
  }

  template <class T>
  const T& load(std::size_t offset = 0) const noexcept {
    return *overlay<T>(offset);
  }

  template <class T, class... Args>
  T& emplace(std::size_t offset, Args&&... args) {
    static_assert(sizeof(T) <= Size, "scratch_pad::emplace: T does not fit.");
    assert(offset <= Size);
    assert(sizeof(T) <= Size - offset);
    T* p = ptr<T>(offset);
    return *std::construct_at(p, std::forward<Args>(args)...);
  }

  template <class T>
  void destroy(std::size_t offset) noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      T* p = ptr<T>(offset);
      std::destroy_at(p);
    }
  }
};

} // namespace arksim

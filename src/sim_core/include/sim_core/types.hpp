#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <vector>

namespace arksim {

using Tick = std::uint64_t;

struct BuffNum {
  double base = 0.0;
  double direct_add = 0.0;
  double direct_mult = 0.0;
  double final_add = 0.0;
  double final_mult = 1.0;

  explicit BuffNum(double base_value = 0.0) : base(base_value) {}

  void reset(double base_value = 0.0) {
    base = base_value;
    clear_modifiers();
  }

  void clear_modifiers() {
    direct_add = 0.0;
    direct_mult = 0.0;
    final_add = 0.0;
    final_mult = 1.0;
  }

  void add_direct_add(double value) { direct_add += value; }
  void add_direct_mult(double value) { direct_mult += value; }
  void add_final_add(double value) { final_add += value; }
  void mul_final_mult(double value) { final_mult *= value; }

  double value() const {
    return ((base + direct_add) * (1.0 + direct_mult) + final_add) * final_mult;
  }

  operator double() const { return value(); }
};

template <typename... Args>
class TriggerProcessor {
public:
  using Fn = std::function<void(Args...)>;
  using Trigger = std::tuple<int, std::uint64_t, Fn>;

  void add(std::uint64_t name, int priority, Fn fn) {
    triggers_[name] = Trigger{priority, name, std::move(fn)};
    dirty_ = true;
  }

  void add(Trigger trigger) {
    const std::uint64_t name = std::get<1>(trigger);
    triggers_[name] = std::move(trigger);
    dirty_ = true;
  }

  void erase(std::uint64_t name) {
    triggers_.erase(name);
    dirty_ = true;
  }

  void clear() {
    triggers_.clear();
    ordered_.clear();
    dirty_ = false;
  }

  bool contains(std::uint64_t name) const {
    return triggers_.find(name) != triggers_.end();
  }

  std::size_t size() const { return triggers_.size(); }

  void operator()(Args... args) const {
    if (triggers_.empty()) {
      return;
    }
    rebuild_ordered_if_needed();
    for (const Trigger* trigger : ordered_) {
      const Fn& fn = std::get<2>(*trigger);
      if (fn) {
        fn(args...);
      }
    }
  }

private:
  void rebuild_ordered_if_needed() const {
    if (!dirty_) {
      return;
    }
    ordered_.clear();
    ordered_.reserve(triggers_.size());
    for (const auto& entry : triggers_) {
      ordered_.push_back(&entry.second);
    }
    std::stable_sort(ordered_.begin(), ordered_.end(), [](const Trigger* a, const Trigger* b) {
      const int pa = std::get<0>(*a);
      const int pb = std::get<0>(*b);
      if (pa != pb) {
        return pa < pb;
      }
      return std::get<1>(*a) < std::get<1>(*b);
    });
    dirty_ = false;
  }

  std::map<std::uint64_t, Trigger> triggers_;
  mutable std::vector<const Trigger*> ordered_;
  mutable bool dirty_ = false;
};

struct UnitId {
  std::uint32_t value = 0;

  constexpr bool operator==(const UnitId& other) const { return value == other.value; }
  constexpr bool operator!=(const UnitId& other) const { return value != other.value; }
  constexpr bool operator<(const UnitId& other) const { return value < other.value; }
};

constexpr UnitId kInvalidUnit{0};

} // namespace arksim

#pragma once

#include <cstdint>
#include <cstdint>
#include <string>

struct lua_State;

namespace arksim {

class SimState;

class ScriptVM {
public:
  ScriptVM();
  ~ScriptVM();

  ScriptVM(const ScriptVM&) = delete;
  ScriptVM& operator=(const ScriptVM&) = delete;

  void bind_world(SimState* world);

  bool load_file(const std::string& path);
  bool has_on_tick() const { return on_tick_ref_ != kNoRef; }
  bool call_on_tick(std::uint64_t tick);

  const std::string& last_error() const { return last_error_; }
  SimState* world() const { return world_; }
  int store_global_function(const std::string& name);
  void release_function(int ref);
  bool call_damage_processor(int ref,
                             std::uint64_t self_id,
                             bool purity,
                             double value,
                             double dmg,
                             std::uint64_t source_id,
                             double& out);

private:
  static constexpr int kNoRef = -1;

  lua_State* state_ = nullptr;
  int on_tick_ref_ = kNoRef;
  SimState* world_ = nullptr; // non-owning
  std::string last_error_;
};

} // namespace arksim

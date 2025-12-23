#include "sim_core/script_vm.hpp"

#include "sim_core/lua_compat.hpp"
#include "sim_core/sim_state.hpp"

#include <cstdint>

namespace arksim {

namespace {

constexpr const char* kVmRegistryKey = "arksim.vm";

ScriptVM* GetVm(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kVmRegistryKey);
  auto* vm = static_cast<ScriptVM*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return vm;
}

int RandU32(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  lua_pushinteger(state, vm && vm->world() ? static_cast<lua_Integer>(vm->world()->rng().next_u32()) : 0);
  return 1;
}

int RandU64(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  lua_pushinteger(state, vm && vm->world() ? static_cast<lua_Integer>(vm->world()->rng().next_u64()) : 0);
  return 1;
}

int RandI32Range(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  const int min = static_cast<int>(luaL_checkinteger(state, 1));
  const int max = static_cast<int>(luaL_checkinteger(state, 2));
  lua_pushinteger(state, vm && vm->world() ? static_cast<lua_Integer>(vm->world()->rng().uniform_i32(min, max)) : 0);
  return 1;
}

int RandU32Range(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  const auto min = static_cast<std::uint32_t>(luaL_checkinteger(state, 1));
  const auto max = static_cast<std::uint32_t>(luaL_checkinteger(state, 2));
  lua_pushinteger(state, vm && vm->world() ? static_cast<lua_Integer>(vm->world()->rng().uniform_u32(min, max)) : 0);
  return 1;
}

int RandF32(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  lua_pushnumber(state, vm && vm->world() ? static_cast<lua_Number>(vm->world()->rng().next_f32()) : 0.0);
  return 1;
}

int RandF64(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  lua_pushnumber(state, vm && vm->world() ? static_cast<lua_Number>(vm->world()->rng().next_f64()) : 0.0);
  return 1;
}

int RandF32Range(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  const float min = static_cast<float>(luaL_checknumber(state, 1));
  const float max = static_cast<float>(luaL_checknumber(state, 2));
  lua_pushnumber(state, vm && vm->world() ? static_cast<lua_Number>(vm->world()->rng().uniform_f32(min, max)) : 0.0);
  return 1;
}

int RandF64Range(lua_State* state) {
  ScriptVM* vm = GetVm(state);
  const double min = static_cast<double>(luaL_checknumber(state, 1));
  const double max = static_cast<double>(luaL_checknumber(state, 2));
  lua_pushnumber(state, vm && vm->world() ? static_cast<lua_Number>(vm->world()->rng().uniform_f64(min, max)) : 0.0);
  return 1;
}

void RegisterGlobals(lua_State* state) {
  lua_register(state, "RandU32", RandU32);
  lua_register(state, "RandU64", RandU64);
  lua_register(state, "RandI32Range", RandI32Range);
  lua_register(state, "RandU32Range", RandU32Range);
  lua_register(state, "RandF32", RandF32);
  lua_register(state, "RandF64", RandF64);
  lua_register(state, "RandF32Range", RandF32Range);
  lua_register(state, "RandF64Range", RandF64Range);
}

} // namespace

ScriptVM::ScriptVM() {
  state_ = luaL_newstate();
  if (state_) {
    luaL_openlibs(state_);
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, kVmRegistryKey);
    RegisterGlobals(state_);
  }
}

ScriptVM::~ScriptVM() {
  if (state_) {
    lua_close(state_);
    state_ = nullptr;
  }
}

void ScriptVM::bind_world(SimState* world) {
  world_ = world;
}

bool ScriptVM::load_file(const std::string& path) {
  last_error_.clear();
  if (on_tick_ref_ >= 0 && state_) {
    luaL_unref(state_, LUA_REGISTRYINDEX, on_tick_ref_);
  }
  on_tick_ref_ = kNoRef;

  if (!state_) {
    last_error_ = "LuaJIT state not initialized";
    return false;
  }

  if (luaL_loadfile(state_, path.c_str()) != 0) {
    const char* msg = lua_tostring(state_, -1);
    last_error_ = msg ? msg : "Failed to load script";
    lua_pop(state_, 1);
    return false;
  }

  if (lua_pcall(state_, 0, 0, 0) != 0) {
    const char* msg = lua_tostring(state_, -1);
    last_error_ = msg ? msg : "Failed to execute script";
    lua_pop(state_, 1);
    return false;
  }

  lua_getglobal(state_, "OnTick");
  if (!lua_isfunction(state_, -1)) {
    lua_pop(state_, 1);
    last_error_ = "OnTick(tick) not found";
    return false;
  }

  on_tick_ref_ = luaL_ref(state_, LUA_REGISTRYINDEX);
  if (on_tick_ref_ < 0) {
    last_error_ = "Failed to store OnTick reference";
    return false;
  }

  return true;
}

bool ScriptVM::call_on_tick(std::uint64_t tick) {
  if (!state_ || on_tick_ref_ < 0) {
    return false;
  }

  lua_rawgeti(state_, LUA_REGISTRYINDEX, on_tick_ref_);
  lua_pushinteger(state_, static_cast<lua_Integer>(tick));

  if (lua_pcall(state_, 1, 0, 0) != 0) {
    const char* msg = lua_tostring(state_, -1);
    last_error_ = msg ? msg : "OnTick execution failed";
    lua_pop(state_, 1);
    return false;
  }

  return true;
}

int ScriptVM::store_global_function(const std::string& name) {
  last_error_.clear();
  if (!state_) {
    last_error_ = "LuaJIT state not initialized";
    return kNoRef;
  }

  lua_getglobal(state_, name.c_str());
  if (!lua_isfunction(state_, -1)) {
    lua_pop(state_, 1);
    last_error_ = "Global function not found: " + name;
    return kNoRef;
  }

  return luaL_ref(state_, LUA_REGISTRYINDEX);
}

void ScriptVM::release_function(int ref) {
  if (!state_ || ref == kNoRef) {
    return;
  }
  luaL_unref(state_, LUA_REGISTRYINDEX, ref);
}

bool ScriptVM::call_damage_processor(int ref,
                                     std::uint64_t self_id,
                                     bool purity,
                                     double value,
                                     double dmg,
                                     std::uint64_t source_id,
                                     double& out) {
  if (!state_ || ref == kNoRef) {
    return false;
  }

  lua_rawgeti(state_, LUA_REGISTRYINDEX, ref);
  lua_pushinteger(state_, static_cast<lua_Integer>(self_id));
  lua_pushboolean(state_, purity ? 1 : 0);
  lua_pushnumber(state_, static_cast<lua_Number>(value));
  lua_pushnumber(state_, static_cast<lua_Number>(dmg));
  if (source_id == 0) {
    lua_pushnil(state_);
  } else {
    lua_pushinteger(state_, static_cast<lua_Integer>(source_id));
  }

  if (lua_pcall(state_, 5, 1, 0) != 0) {
    const char* msg = lua_tostring(state_, -1);
    last_error_ = msg ? msg : "Lua custom damage processor failed";
    lua_pop(state_, 1);
    return false;
  }

  if (!lua_isnumber(state_, -1)) {
    last_error_ = "Lua custom damage processor returned non-number";
    lua_pop(state_, 1);
    return false;
  }

  out = static_cast<double>(lua_tonumber(state_, -1));
  lua_pop(state_, 1);
  return true;
}

} // namespace arksim

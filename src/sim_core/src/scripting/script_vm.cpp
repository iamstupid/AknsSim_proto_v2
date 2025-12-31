#include "sim_core/scripting/script_vm.hpp"

#include "sim_core/scripting/lua_compat.hpp"
#include "sim_core/runtime/sim_state.hpp"

#include <cstdint>
#include <filesystem>

namespace arksim {

namespace {

constexpr const char* kVmRegistryKey = "arksim.vm";

void remove_global(lua_State* state, const char* name) {
  lua_pushnil(state);
  lua_setglobal(state, name);
}

void remove_package_loaded(lua_State* state, const char* name) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "loaded");
  if (lua_istable(state, -1)) {
    lua_pushnil(state);
    lua_setfield(state, -2, name);
  }
  lua_pop(state, 2); // loaded + package
}

void remove_package_preload(lua_State* state, const char* name) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  lua_getfield(state, -1, "preload");
  if (lua_istable(state, -1)) {
    lua_pushnil(state);
    lua_setfield(state, -2, name);
  }
  lua_pop(state, 2); // preload + package
}

void set_package_field_string(lua_State* state, const char* field, const char* value) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_pushstring(state, value);
  lua_setfield(state, -2, field);
  lua_pop(state, 1);
}

void remove_package_field(lua_State* state, const char* field) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }
  lua_pushnil(state);
  lua_setfield(state, -2, field);
  lua_pop(state, 1);
}

void restrict_package_loaders_to_lua_only(lua_State* state) {
  lua_getglobal(state, "package");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    return;
  }

  const char* field = "loaders"; // LuaJIT / Lua 5.1
  lua_getfield(state, -1, field);
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2); // package + nil
    return;
  }

  // Keep:
  //  1) preload loader
  //  2) Lua loader
  lua_rawgeti(state, -1, 1); // loader1
  lua_rawgeti(state, -2, 2); // loader2

  lua_createtable(state, 2, 0); // new_loaders
  lua_pushvalue(state, -3);     // loader1
  lua_rawseti(state, -2, 1);
  lua_pushvalue(state, -3); // loader2
  lua_rawseti(state, -2, 2);

  // package.loaders = new_loaders
  lua_setfield(state, -5, field);

  lua_pop(state, 4); // package + old_loaders + loader1 + loader2
}

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

    // Determinism sandbox:
    // - allow Lua modules via `require` (pure Lua only)
    // - disallow host-dependent libs and native module loading
    remove_global(state_, "io");
    remove_global(state_, "os");
    remove_global(state_, "debug");
    remove_global(state_, "jit");

    remove_package_loaded(state_, "io");
    remove_package_loaded(state_, "os");
    remove_package_loaded(state_, "debug");
    remove_package_loaded(state_, "jit");

    remove_package_preload(state_, "io");
    remove_package_preload(state_, "os");
    remove_package_preload(state_, "debug");
    remove_package_preload(state_, "jit");

    // Disable native module loading (.dll/.so) and lock module search to Lua loaders.
    set_package_field_string(state_, "cpath", "");
    remove_package_field(state_, "loadlib");
    restrict_package_loaders_to_lua_only(state_);

    // Remove non-deterministic RNG to steer scripts toward sim RNG.
    lua_getglobal(state_, "math");
    if (lua_istable(state_, -1)) {
      lua_pushnil(state_);
      lua_setfield(state_, -2, "random");
      lua_pushnil(state_);
      lua_setfield(state_, -2, "randomseed");
    }
    lua_pop(state_, 1);

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

  // Restrict module search path to the script directory for stable, reproducible requires.
  {
    namespace fs = std::filesystem;
    fs::path p = fs::path(path).lexically_normal();
    fs::path dir = p.parent_path();
    std::string dir_str = dir.empty() ? std::string(".") : dir.generic_string();
    std::string lua_path = dir_str + "/?.lua;" + dir_str + "/?/init.lua";
    set_package_field_string(state_, "path", lua_path.c_str());
    set_package_field_string(state_, "cpath", "");
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

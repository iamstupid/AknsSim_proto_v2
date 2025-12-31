#pragma once

#if defined(__has_include)
  #if __has_include(<luajit/lua.hpp>)
    #include <luajit/lua.hpp>
  #elif __has_include(<luajit-2.1/lua.hpp>)
    #include <luajit-2.1/lua.hpp>
  #elif __has_include(<lua.hpp>)
    #include <lua.hpp>
  #elif __has_include(<luajit/lua.h>)
    extern "C" {
    #include <luajit/lua.h>
    #include <luajit/lauxlib.h>
    #include <luajit/lualib.h>
    }
  #elif __has_include(<luajit-2.1/lua.h>)
    extern "C" {
    #include <luajit-2.1/lua.h>
    #include <luajit-2.1/lauxlib.h>
    #include <luajit-2.1/lualib.h>
    }
  #elif __has_include(<lua.h>)
    extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
    }
  #else
    #error "Lua headers not found. Ensure LuaJIT is installed and include paths are set."
  #endif
#else
  extern "C" {
  #include <luajit-2.1/lua.h>
  #include <luajit-2.1/lauxlib.h>
  #include <luajit-2.1/lualib.h>
  }
#endif

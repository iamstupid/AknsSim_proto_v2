cmake_minimum_required(VERSION 3.20)

project(arksim_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARKSIM_BUILD_GAME "Build SDL-based game host (sim_game)" OFF)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(nlohmann_json CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(LuaJIT REQUIRED)

add_subdirectory(ECS-Lab)

add_library(sim_core
  src/sim_core/src/core/rng.cpp
  src/sim_core/src/effects/effect.cpp
  src/sim_core/src/runtime/sim_state.cpp
  src/sim_core/src/scripting/script_vm.cpp
  src/sim_core/src/nav/map.cpp
  src/sim_core/src/nav/bresenham_cache.cpp
  src/sim_core/src/nav/path_map.cpp
  src/sim_core/src/spatial/spatial_grid.cpp
  src/sim_core/src/effects/effects.cpp
  src/sim_core/src/spatial/target_selector.cpp
  src/sim_core/src/components/damage.cpp
  src/sim_core/src/components/attack.cpp
  src/sim_core/src/components/buff.cpp
  src/sim_core/src/components/barrier_shield.cpp
  src/sim_core/src/components/hp.cpp
  src/sim_core/src/components/position.cpp
  src/sim_core/src/components/projectile.cpp
  src/sim_core/src/components/route_move.cpp
  src/sim_core/src/components/unbalance.cpp
)
target_include_directories(sim_core
  PUBLIC
    src/sim_core/include
)
target_link_libraries(sim_core
  PUBLIC
    ecs_lab
    LuaJIT::LuaJIT
)

if (MSVC)
  target_compile_options(sim_core PUBLIC /fp:precise)
endif()

add_executable(sim_cli
  apps/sim_cli/main.cpp
)
target_link_libraries(sim_cli
  PRIVATE
    sim_core
    nlohmann_json::nlohmann_json
)

if (ARKSIM_BUILD_GAME)
  find_package(SDL2 CONFIG REQUIRED)
  add_executable(sim_game
    apps/sim_game/main.cpp
  )
  target_link_libraries(sim_game
    PRIVATE
      sim_core
      $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
      $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
  )
endif()

add_executable(sim_tests
  tests/test_sim_core.cpp
)
target_link_libraries(sim_tests
  PRIVATE
    sim_core
    doctest::doctest
)
add_test(NAME sim_tests COMMAND sim_tests)

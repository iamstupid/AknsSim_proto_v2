cmake_minimum_required(VERSION 3.20)

project(arksim_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(SDL2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(LuaJIT REQUIRED)

set(FLECS_STATIC ON CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/flecs-4.1.4 EXCLUDE_FROM_ALL)

add_library(sim_core
  src/sim_core/src/rng.cpp
  src/sim_core/src/effect.cpp
  src/sim_core/src/sim_state.cpp
  src/sim_core/src/script_vm.cpp
  src/sim_core/src/components/damage.cpp
  src/sim_core/src/components/buff.cpp
  src/sim_core/src/components/barrier_shield.cpp
  src/sim_core/src/components/hp.cpp
)
target_include_directories(sim_core
  PUBLIC
    src/sim_core/include
)
target_link_libraries(sim_core
  PUBLIC
    flecs::flecs_static
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

add_executable(sim_game
  apps/sim_game/main.cpp
)
target_link_libraries(sim_game
  PRIVATE
    sim_core
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)

add_executable(sim_tests
  tests/test_sim_core.cpp
)
target_link_libraries(sim_tests
  PRIVATE
    sim_core
    doctest::doctest
)
add_test(NAME sim_tests COMMAND sim_tests)

add_subdirectory(ECS-Lab)

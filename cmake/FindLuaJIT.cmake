# - Try to find LuaJIT
# Once done, this will define
#  LUAJIT_FOUND
#  LUAJIT_INCLUDE_DIRS
#  LUAJIT_LIBRARIES
#  LuaJIT::LuaJIT target

find_path(LUAJIT_INCLUDE_DIR
  NAMES lua.hpp lua.h
  PATH_SUFFIXES luajit luajit-2.1
)

find_library(LUAJIT_LIBRARY_RELEASE
  NAMES lua51 luajit-5.1 luajit
  PATH_SUFFIXES lib
)

find_library(LUAJIT_LIBRARY_DEBUG
  NAMES lua51d lua51 luajit-5.1d luajit-5.1 luajit
  PATH_SUFFIXES debug/lib lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LuaJIT
  REQUIRED_VARS LUAJIT_INCLUDE_DIR LUAJIT_LIBRARY_RELEASE
)

if (LUAJIT_FOUND)
  set(LUAJIT_INCLUDE_DIRS ${LUAJIT_INCLUDE_DIR})
  set(LUAJIT_LIBRARIES ${LUAJIT_LIBRARY_RELEASE})

  if (NOT TARGET LuaJIT::LuaJIT)
    add_library(LuaJIT::LuaJIT UNKNOWN IMPORTED)
    set_target_properties(LuaJIT::LuaJIT PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_INCLUDE_DIR}"
    )
    if (LUAJIT_LIBRARY_DEBUG)
      set_target_properties(LuaJIT::LuaJIT PROPERTIES
        IMPORTED_LOCATION_DEBUG "${LUAJIT_LIBRARY_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${LUAJIT_LIBRARY_RELEASE}"
      )
    else()
      set_target_properties(LuaJIT::LuaJIT PROPERTIES
        IMPORTED_LOCATION "${LUAJIT_LIBRARY_RELEASE}"
      )
    endif()
  endif()
endif()

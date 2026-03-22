# Find libb2
# Defines:
# libb2_FOUND
# libb2::libb2

find_path(LIBB2_INCLUDE_DIR NAMES blake2.h
    PATHS "${CMAKE_SOURCE_DIR}/../deps/vcpkg/installed/x64-windows/include"
    NO_DEFAULT_PATH
)
# Fallback to default
find_path(LIBB2_INCLUDE_DIR NAMES blake2.h)

find_library(LIBB2_LIBRARY NAMES b2 libb2
    PATHS "${CMAKE_SOURCE_DIR}/../deps/vcpkg/installed/x64-windows/lib"
    NO_DEFAULT_PATH
)
# Fallback
find_library(LIBB2_LIBRARY NAMES b2 libb2)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libb2 DEFAULT_MSG LIBB2_LIBRARY LIBB2_INCLUDE_DIR)

if(libb2_FOUND)
  if(NOT TARGET libb2::libb2)
    add_library(libb2::libb2 UNKNOWN IMPORTED)
    set_target_properties(libb2::libb2 PROPERTIES
      IMPORTED_LOCATION "${LIBB2_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${LIBB2_INCLUDE_DIR}")
  endif()
endif()

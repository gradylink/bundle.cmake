include(${CMAKE_CURRENT_LIST_DIR}/utils.cmake)

set(_BUNDLE_INTERNAL_DIR "${CMAKE_CURRENT_LIST_DIR}")

option(BUNDLE_USE_ZLIB "Decompress DEFLATE bundle entries with the system zlib instead of miniz" OFF)

function(_bundle_ensure_catalog)
  if(COMMAND cl_add_dep)
    return()
  endif()
  include(${_BUNDLE_INTERNAL_DIR}/cl-bootstrap.cmake)
endfunction()

function(_bundle_ensure_runtime_target)
  if(TARGET bundle_runtime)
    return()
  endif()

  _bundle_ensure_catalog()
  cl_repo()

  add_library(bundle_runtime STATIC "${_BUNDLE_ROOT_DIR}/src/bundle_runtime.c")
  target_include_directories(bundle_runtime PUBLIC "${_BUNDLE_ROOT_DIR}/src")

  if(BUNDLE_USE_ZLIB)
    cl_add_dep(bundle_runtime zlib PUBLIC)
    target_compile_definitions(bundle_runtime PUBLIC BUNDLE_USE_ZLIB=1)
    _bundle_log(STATUS "using zlib (via Catalog) for DEFLATE decompression")
  else()
    cl_add_dep(bundle_runtime miniz PUBLIC STATIC)
    _bundle_log(STATUS "using miniz (via Catalog) for DEFLATE decompression")
  endif()
endfunction()

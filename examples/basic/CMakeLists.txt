cmake_minimum_required(VERSION 3.18)
project(bundle_example_basic C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bundle.cmake)

bundle_add(assets
  DIRECTORIES assets
)

add_executable(basic main.c)
target_link_libraries(basic PRIVATE assets)

cmake_minimum_required(VERSION 3.18)
project(bundle_example_cpp_streams CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bundle.cmake)

bundle_add(poems
  DIRECTORIES assets
)

add_executable(cpp_streams main.cpp)
target_link_libraries(cpp_streams PRIVATE poems)

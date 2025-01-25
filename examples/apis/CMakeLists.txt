cmake_minimum_required(VERSION 3.14)
project(hot100 VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)

add_executable(apis "")


target_sources(apis
  PRIVATE
  "apis.cc"
)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "."
)
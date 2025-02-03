cmake_minimum_required(VERSION 3.14)
project(hot100 VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)

add_executable(webserver "")
target_sources(webserver
  PRIVATE
  "threadpool.h"
  "httpconn.h"
  "httpconn.cc"
  "web.cc"
)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "."
)
cmake_minimum_required(VERSION 3.14)
project(hot100 VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)

add_executable(client "")
add_executable(client_epoll "")
add_executable(server "")
add_executable(server_epoll "")
target_sources(client
  PRIVATE
  "client.cc"
)

target_sources(client_epoll
  PRIVATE
  "client_epoll.cc"
)

target_sources(server
  PRIVATE
  "server.cc"
)


target_sources(server_epoll
  PRIVATE
  "server_epoll.cc"
)
include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "."
)
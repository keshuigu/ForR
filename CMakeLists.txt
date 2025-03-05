cmake_minimum_required(VERSION 3.14)
project(hot100 VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(examples/apis)
add_subdirectory(examples/chatroom)
add_subdirectory(examples/processpools)
add_subdirectory(examples/threadpools)
add_subdirectory(examples/redis)
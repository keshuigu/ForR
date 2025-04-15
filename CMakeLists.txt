cmake_minimum_required(VERSION 3.14)
project(ForR VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

# demos
add_subdirectory(examples/apis)
add_subdirectory(examples/chatroom)
add_subdirectory(examples/processpools)
add_subdirectory(examples/threadpools)
add_subdirectory(examples/redis)
# demos

add_library(forr "")

target_sources(forr
  PUBLIC
    "data/reader.cc"
)

include_directories( forr
  PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/FitSDKRelease_21.158.00/cpp"
    "."
)

# TEST

enable_testing()
set(install_gtest OFF)

add_subdirectory("third_party/googletest")

add_executable(forr-tests "")
target_sources(forr-tests
  PUBLIC
    "tests/test.cc"
)
target_link_libraries(forr-tests forr gtest gtest_main)
add_test(NAME "algorithm-template-tests" COMMAND "algorithm-template-tests")

# app
add_executable(forr-app "")
target_sources(forr-app
  PUBLIC
    "launch/app.cc"
)
target_link_libraries(forr-app forr)
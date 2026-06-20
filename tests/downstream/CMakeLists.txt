cmake_minimum_required(VERSION 3.16)

project(jaggedcpp_downstream_smoke LANGUAGES CXX)

find_package(jaggedcpp CONFIG REQUIRED)

if(NOT TARGET jagged::jagged)
  message(FATAL_ERROR "jaggedcpp package did not provide jagged::jagged")
endif()
if(NOT TARGET awkward::awkward)
  message(FATAL_ERROR "jaggedcpp package did not provide awkward::awkward")
endif()

add_executable(jaggedcpp_downstream_smoke main.cpp)
target_link_libraries(jaggedcpp_downstream_smoke PRIVATE awkward::awkward)

enable_testing()
add_test(NAME jaggedcpp_downstream_smoke COMMAND jaggedcpp_downstream_smoke)

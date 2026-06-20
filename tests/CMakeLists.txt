function(add_jagged_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE jagged::jagged)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_jagged_test(test_core)
add_jagged_test(test_operations)
add_jagged_test(test_optional)
add_jagged_test(test_awkward_layout)
add_jagged_test(test_awkward_indexing)
add_jagged_test(test_awkward_missing)
add_jagged_test(test_awkward_reducers)
add_jagged_test(test_awkward_records)
add_jagged_test(test_awkward_broadcasting)
add_jagged_test(test_awkward_buffers)
add_jagged_test(test_awkward_builder)
add_jagged_test(test_awkward_strings)
add_jagged_test(test_awkward_invariants)
add_jagged_test(test_awkward_concurrency)
find_package(Threads REQUIRED)
target_link_libraries(test_awkward_concurrency PRIVATE Threads::Threads)

configure_file(
  run_downstream.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/run_downstream.cmake
  @ONLY)
add_test(
  NAME test_downstream_install
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/run_downstream.cmake)

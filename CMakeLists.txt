cmake_minimum_required(VERSION 3.16)

project(
  jaggedcpp
  VERSION 0.1.0
  DESCRIPTION "A header-only C++20 Awkward-style jagged array library"
  LANGUAGES CXX)

include(GNUInstallDirs)

add_library(jagged INTERFACE)
add_library(jagged::jagged ALIAS jagged)
add_library(awkward::awkward ALIAS jagged)

target_compile_features(jagged INTERFACE cxx_std_20)
target_include_directories(
  jagged
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

option(JAGGEDCPP_BUILD_EXAMPLES "Build jaggedcpp examples" ON)
option(JAGGEDCPP_BUILD_TESTS "Build jaggedcpp tests" ON)
option(JAGGEDCPP_AWKWARD_GOLDEN_TESTS "Run Python Awkward golden fixture generation tests" OFF)
option(JAGGEDCPP_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(JAGGEDCPP_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jagged INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(jagged INTERFACE -fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "JAGGEDCPP_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
endif()

if(JAGGEDCPP_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

include(CTest)
if(BUILD_TESTING AND JAGGEDCPP_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_TESTING AND JAGGEDCPP_AWKWARD_GOLDEN_TESTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(
    NAME generate_awkward_goldens
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_awkward_goldens.py --output-dir
      ${CMAKE_CURRENT_BINARY_DIR}/awkward-goldens)
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS jagged EXPORT jaggedTargets)
install(
  EXPORT jaggedTargets
  NAMESPACE jagged::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jaggedcpp)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/jaggedcppConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/jaggedcppConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jaggedcpp)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/jaggedcppConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/jaggedcppConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/jaggedcppConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/jaggedcpp)

cmake_minimum_required(VERSION 3.16)
project(csvq VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Core library, shared by the CLI and the tests.
add_library(csvq_core
  src/csv.cpp
  src/query.cpp
  src/format.cpp
)
target_include_directories(csvq_core PUBLIC include)

if(MSVC)
  target_compile_options(csvq_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(csvq_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

add_executable(csvq src/main.cpp)
target_link_libraries(csvq PRIVATE csvq_core)

option(CSVQ_BUILD_TESTS "Build unit tests" ON)
if(CSVQ_BUILD_TESTS)
  enable_testing()
  add_executable(csvq_tests tests/test_main.cpp)
  target_link_libraries(csvq_tests PRIVATE csvq_core)
  add_test(NAME unit_tests COMMAND csvq_tests)
  # End-to-end smoke test of the CLI against the bundled sample data.
  add_test(NAME cli_smoke
    COMMAND csvq -g region -a "count,sum:revenue" -o "sum_revenue:desc"
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/sales.csv)
endif()

install(TARGETS csvq RUNTIME DESTINATION bin)

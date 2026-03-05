cmake_minimum_required(VERSION 3.20)
project(HyperSearchChess LANGUAGES CXX)

option(HYPERCHESS_ENABLE_SANITIZERS "Enable Address/UB sanitizers in debug builds" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_library(chess_core
    src/bitboard.cpp
    src/evaluation.cpp
    src/move.cpp
    src/position.cpp
    src/search.cpp
    src/uci.cpp
    src/transposition_table.cpp
)

target_include_directories(chess_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(chess_core
    PRIVATE
        $<$<CONFIG:Release>:-Ofast -march=native -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O3 -march=native>
        $<$<CONFIG:Debug>:-O0 -g3>
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor
        -Wold-style-cast -Woverloaded-virtual -Wnull-dereference
        $<$<CXX_COMPILER_ID:GNU>:-Wduplicated-cond -Wduplicated-branches -Wlogical-op>
)

if(HYPERCHESS_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(chess_core PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
  target_link_options(chess_core PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
  set_property(TARGET chess_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

add_executable(hyperchess src/main.cpp)
target_link_libraries(hyperchess PRIVATE chess_core)

add_executable(perft src/perft.cpp)
target_link_libraries(perft PRIVATE chess_core)

add_executable(bench src/bench.cpp)
target_link_libraries(bench PRIVATE chess_core)

if(ipo_supported)
  set_property(TARGET hyperchess PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  set_property(TARGET perft PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  set_property(TARGET bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

cmake_minimum_required(VERSION 3.20)

project(Daedalus
        VERSION 1.0.0
        DESCRIPTION "A C++20 object-oriented data structures and algorithms library"
        HOMEPAGE_URL "https://github.com/AYUSHSAINI9876/Daedalus"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(DAEDALUS_BUILD_TESTS      "Build the unit test suite"        ON)
option(DAEDALUS_BUILD_EXAMPLES   "Build the interactive CLI"        ON)
option(DAEDALUS_BUILD_BENCHMARKS "Build the benchmark harness"      ON)
option(DAEDALUS_WERROR           "Treat compiler warnings as errors" OFF)
option(DAEDALUS_SANITIZE         "Enable address+UB sanitizers"      OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # feeds clangd / VS Code IntelliSense

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# The library itself: header-only, exported as an INTERFACE target
# ---------------------------------------------------------------------------
add_library(daedalus INTERFACE)
add_library(Daedalus::daedalus ALIAS daedalus)

target_include_directories(daedalus INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(daedalus INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# Shared warning / sanitizer settings for everything we compile ourselves
# ---------------------------------------------------------------------------
add_library(daedalus_build_flags INTERFACE)

if(MSVC)
    target_compile_options(daedalus_build_flags INTERFACE /W4 /permissive- /EHsc)
    if(DAEDALUS_WERROR)
        target_compile_options(daedalus_build_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(daedalus_build_flags INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference
        -Wdouble-promotion -Wcast-align -Wunused)
    if(DAEDALUS_WERROR)
        target_compile_options(daedalus_build_flags INTERFACE -Werror)
    endif()
    if(DAEDALUS_SANITIZE)
        target_compile_options(daedalus_build_flags INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(daedalus_build_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(DAEDALUS_BUILD_TESTS)
    enable_testing()

    file(GLOB DAEDALUS_TEST_SOURCES CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)

    add_executable(daedalus_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp
                                  ${DAEDALUS_TEST_SOURCES})
    target_link_libraries(daedalus_tests PRIVATE daedalus daedalus_build_flags)
    target_include_directories(daedalus_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    # The server and thread-pool tests need real threads and sockets.
    find_package(Threads REQUIRED)
    target_link_libraries(daedalus_tests PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(daedalus_tests PRIVATE ws2_32)
    endif()

    # One CTest entry per suite, so a failure names the area that broke.
    #
    # This list must match the suite names in the test sources exactly: the
    # runner exits non-zero when a filter matches nothing, which turns a typo or
    # a renamed suite into a red build instead of a silently skipped one.
    # Refresh it with:  ./build/bin/daedalus_tests --list | cut -d. -f1 | sort -u
    set(DAEDALUS_SUITES
        # core containers
        DynamicArray SinglyLinkedList DoublyLinkedList Deque Stack Queue
        CircularBuffer SkipList
        # trees and indexed structures
        BinarySearchTree AVLTree RedBlackTree SplayTree Treap BTree BinaryTree
        BinaryHeap PriorityQueue SegmentTree FenwickTree Trie
        # hashing, caching and sets
        HashMap HashSet BloomFilter LRUCache LFUCache DisjointSet
        # graphs
        Graph GraphTraversal ShortestPath MinimumSpanningTree Connectivity
        NetworkFlow Matching
        # algorithms
        Sorting Searching Strings DynamicProgramming Backtracking Greedy
        NumberTheory DivideAndConquer
        # cross-cutting design
        Polymorphism
        # application layer
        Crypto Auth Json Http Router ThreadPool Server)

    foreach(suite IN LISTS DAEDALUS_SUITES)
        add_test(NAME ${suite}
                 COMMAND daedalus_tests --filter=${suite}.)
        set_tests_properties(${suite} PROPERTIES
            FAIL_REGULAR_EXPRESSION "RESULT: FAIL"
            TIMEOUT 120)
    endforeach()

    # A single entry that runs everything, useful as a one-line gate in CI.
    add_test(NAME AllTests COMMAND daedalus_tests)
    set_tests_properties(AllTests PROPERTIES TIMEOUT 300)
endif()

# ---------------------------------------------------------------------------
# Interactive CLI demo
# ---------------------------------------------------------------------------
if(DAEDALUS_BUILD_EXAMPLES)
    add_executable(daedalus_cli ${CMAKE_CURRENT_SOURCE_DIR}/examples/daedalus_cli.cpp)
    target_link_libraries(daedalus_cli PRIVATE daedalus daedalus_build_flags)

    # The web playground: HTTP server + auth + JSON API over the library.
    add_executable(daedalus_server ${CMAKE_CURRENT_SOURCE_DIR}/examples/daedalus_server.cpp)
    target_link_libraries(daedalus_server PRIVATE daedalus daedalus_build_flags)
    find_package(Threads REQUIRED)
    target_link_libraries(daedalus_server PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(daedalus_server PRIVATE ws2_32)
    endif()
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(DAEDALUS_BUILD_BENCHMARKS)
    add_executable(daedalus_bench ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/benchmark_main.cpp)
    target_link_libraries(daedalus_bench PRIVATE daedalus daedalus_build_flags)
endif()

# ---------------------------------------------------------------------------
# Install / package config so downstream projects can find_package(Daedalus)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS daedalus EXPORT DaedalusTargets)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/daedalus
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT DaedalusTargets
        FILE DaedalusTargets.cmake
        NAMESPACE Daedalus::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Daedalus)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DaedalusConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/DaedalusConfig.cmake"
"include(\"\${CMAKE_CURRENT_LIST_DIR}/DaedalusTargets.cmake\")\n")

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DaedalusConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DaedalusConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Daedalus)

message(STATUS "Daedalus ${PROJECT_VERSION}: tests=${DAEDALUS_BUILD_TESTS} "
               "examples=${DAEDALUS_BUILD_EXAMPLES} benchmarks=${DAEDALUS_BUILD_BENCHMARKS} "
               "build=${CMAKE_BUILD_TYPE}")

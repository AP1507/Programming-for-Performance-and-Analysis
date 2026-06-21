cmake_minimum_required(VERSION 3.16)
project(programming_for_performance_analysis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(pc_demo
    src/producer_consumer.cpp
)

add_executable(cache_sim
    src/cache_sim.cpp
)

add_executable(false_sharing_bench
    src/false_sharing_bench.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(pc_demo PRIVATE Threads::Threads)
target_link_libraries(false_sharing_bench PRIVATE Threads::Threads)

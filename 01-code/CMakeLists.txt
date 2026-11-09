cmake_minimum_required(VERSION 3.20)
project(didar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti)

find_package(Threads REQUIRED)

add_library(didar src/wire.cpp src/client.cpp)
target_include_directories(didar PUBLIC include PRIVATE src)

add_executable(didard src/didard.cpp src/config.cpp)
target_include_directories(didard PRIVATE src)
target_link_libraries(didard PRIVATE didar Threads::Threads)

add_executable(didar-agent src/didar_agent.cpp src/config.cpp)
target_include_directories(didar-agent PRIVATE src)
target_link_libraries(didar-agent PRIVATE didar)

enable_testing()
add_subdirectory(tests)

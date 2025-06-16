cmake_minimum_required(VERSION 3.31)
project(beatcpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ncurses
find_package(Curses REQUIRED)
include_directories(${Curses_INCLUDE_DIRS})

# Set source
add_executable(beatcpp main.cpp)

# Link libraries
target_link_libraries(beatcpp ncursesw)
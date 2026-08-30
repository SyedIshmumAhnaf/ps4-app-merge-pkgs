cmake_minimum_required(VERSION 3.10)
project(splitter)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(splitter main.cpp splitter_core.cpp)

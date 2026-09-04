cmake_minimum_required(VERSION 3.13)
project(splitter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_executable(splitter
    main.cpp
    splitter_core.cpp
    ../common/sha256.cpp
    ../common/manifest.cpp
)

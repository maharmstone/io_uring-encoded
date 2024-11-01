cmake_minimum_required(VERSION 3.12)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

enable_language(ASM)

add_executable(read-test src/read-test.cpp
    src/dumps.S)

add_executable(write-test src/write-test.cpp
    src/dumps2.S)

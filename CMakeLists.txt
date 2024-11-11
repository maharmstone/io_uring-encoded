cmake_minimum_required(VERSION 3.12)

project(io_uring-encoded
    LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

enable_language(ASM)

add_executable(read-test src/read-test.cpp
    src/dumps.S)
target_include_directories(read-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS read-test
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_executable(write-test src/write-test.cpp
    src/dumps2.S)
target_include_directories(write-test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS write-test
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

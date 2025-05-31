cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(futex-sample C)

add_executable(futex-sample-fork src/fork-ver.c)
add_executable(futex-sample-pthread src/pthread-ver.c)

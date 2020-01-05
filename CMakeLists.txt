cmake_minimum_required(VERSION 3.13)
project(jlmalloc)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(jlmalloc
        Makefile
        memlib.h
        mm.c
        mm.h)

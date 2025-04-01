cmake_minimum_required(VERSION 3.22)
project(EX4_OS)

set(CMAKE_CXX_STANDARD 11)

add_executable(EX4_OS
        VirtualMemory.cpp
        VirtualMemory.h
        MemoryConstants.h
        PhysicalMemory.h)

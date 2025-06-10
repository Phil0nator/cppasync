cmake_minimum_required(VERSION 3.29)

# Project name and version
project(cppasync VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)



option(BUILD_EXAMPLES "Build example programs" ON) 
if(BUILD_EXAMPLES) 
    add_subdirectory(examples)
endif()

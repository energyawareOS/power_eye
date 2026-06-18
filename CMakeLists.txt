cmake_minimum_required(VERSION 3.22)
project(power_eye VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

add_subdirectory(src)

option(USE_NVML "Build with NVIDIA NVML support" ON)
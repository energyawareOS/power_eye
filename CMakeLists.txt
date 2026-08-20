cmake_minimum_required(VERSION 3.22)
project(power_eye VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

# Must be declared before add_subdirectory() so src/ sees it.
option(USE_NVML "Build with NVIDIA NVML support" OFF)

add_subdirectory(src)

cmake_minimum_required(VERSION 3.22)
project(power_eye VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

# Must be declared before add_subdirectory() so src/ sees it.
option(USE_NVML "Build with NVIDIA NVML support" OFF)

add_subdirectory(src)

enable_testing()
add_executable(test_adaptive_core tests/test_adaptive_core.c src/adaptive_core.c)
target_link_libraries(test_adaptive_core PRIVATE m)
add_test(NAME adaptive_core COMMAND test_adaptive_core)

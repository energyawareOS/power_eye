set(SRC_FILES
        monitor.c
        ipmi.c
        main.c
        nvml.c
        powercap.c
        timeutil.c
        report.c
)

add_executable(power_eye ${SRC_FILES})

if (USE_NVML)
    find_package(CUDAToolkit REQUIRED)
    add_compile_definitions(HAVE_NVML)
    target_link_libraries(power_eye PRIVATE CUDA::nvml)
else ()
    target_compile_definitions(power_eye PRIVATE NO_NVML)
endif ()
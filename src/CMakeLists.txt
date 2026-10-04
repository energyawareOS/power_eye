set(SRC_FILES
        monitor.c
        ipmi.c
        main.c
        nvml.c
        powercap.c
        timeutil.c
        report.c
        cpu_usage.c
        proc_usage.c
        proc_usage.h
        gpu_usage.c
        self_overhead.c
        adaptive.c
        adaptive_core.c
        coma.c
        coma_core.c
        bpf_sampler.c
        bpf_sampler.h
)

add_executable(power_eye ${SRC_FILES})

if (USE_BPF)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBBPF REQUIRED libbpf)
    find_program(CLANG_EXECUTABLE clang REQUIRED)
    if (CMAKE_LIBRARY_ARCHITECTURE)
        set(BPF_ARCH_INCLUDE -I/usr/include/${CMAKE_LIBRARY_ARCHITECTURE})
    endif ()
    set(BPF_OBJ ${CMAKE_CURRENT_BINARY_DIR}/power_eye.bpf.o)
    # -mcpu=v3: the rate limiter uses an atomic compare-and-swap
    add_custom_command(
        OUTPUT ${BPF_OBJ}
        COMMAND ${CLANG_EXECUTABLE} -O2 -g -target bpf -mcpu=v3 -D__TARGET_ARCH_x86
                ${BPF_ARCH_INCLUDE} -I${CMAKE_CURRENT_SOURCE_DIR}/bpf
                -c ${CMAKE_CURRENT_SOURCE_DIR}/bpf/power_eye.bpf.c -o ${BPF_OBJ}
        DEPENDS bpf/power_eye.bpf.c bpf/sampler_shared.h
        COMMENT "Compiling the BPF object")
    target_sources(power_eye PRIVATE bpf_embed.c)
    set_source_files_properties(bpf_embed.c PROPERTIES
        OBJECT_DEPENDS ${BPF_OBJ}
        COMPILE_DEFINITIONS "BPF_OBJ_PATH=\"${BPF_OBJ}\"")
    target_compile_definitions(power_eye PRIVATE HAVE_BPF)
    target_include_directories(power_eye PRIVATE ${LIBBPF_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(power_eye PRIVATE ${LIBBPF_LIBRARIES})
endif ()

if (USE_NVML)
    find_package(CUDAToolkit REQUIRED)
    add_compile_definitions(HAVE_NVML)
    target_link_libraries(power_eye PRIVATE CUDA::nvml)
else ()
    target_compile_definitions(power_eye PRIVATE NO_NVML)
endif ()
/* Embeds the compiled BPF object into the executable. BPF_OBJ_PATH is set by
 * CMake; bpf_sampler.c opens the object from memory. */
__asm__(".section .rodata\n"
        ".balign 8\n"
        ".global pe_bpf_obj_start\n"
        "pe_bpf_obj_start:\n"
        ".incbin \"" BPF_OBJ_PATH "\"\n"
        ".global pe_bpf_obj_end\n"
        "pe_bpf_obj_end:\n"
        ".previous\n");

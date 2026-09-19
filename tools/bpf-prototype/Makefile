CLANG ?= clang
CFLAGS ?= -O2 -g -Wall
ARCH := $(shell uname -m)

all: idle_sampler idle_sampler.bpf.o

idle_sampler: idle_sampler.c idle_sampler.h
	$(CC) $(CFLAGS) -o $@ $< -lbpf

# -mcpu=v3: the rate limiter uses an atomic compare-and-swap
idle_sampler.bpf.o: idle_sampler.bpf.c idle_sampler.h
	$(CLANG) -O2 -g -target bpf -mcpu=v3 -D__TARGET_ARCH_x86 -I/usr/include/$(ARCH)-linux-gnu -c $< -o $@

clean:
	rm -f idle_sampler idle_sampler.bpf.o

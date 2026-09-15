CLANG ?= clang
CFLAGS ?= -O2 -g -Wall
ARCH := $(shell uname -m)

all: rapl_probe rapl_probe.bpf.o

rapl_probe: rapl_probe.c
	$(CC) $(CFLAGS) -o $@ $< -lbpf

rapl_probe.bpf.o: rapl_probe.bpf.c
	$(CLANG) -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/$(ARCH)-linux-gnu -c $< -o $@

clean:
	rm -f rapl_probe rapl_probe.bpf.o

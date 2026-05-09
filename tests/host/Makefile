# Layer 1 host tests for the AGC core. Compiles the upstream yaAGC engine
# files, our agc_init.c, and a tiny host-side IO stub with mingw gcc; runs
# in well under a second per test. No ESP-IDF, no FreeRTOS, no channel
# router — the goal is to exercise the engine + ROM-loader in isolation.
#
# Run from this directory:
#   mingw32-make run        # build + run all test binaries
#   mingw32-make clean

CC      := gcc
CFLAGS  ?= -O2 -g -Wall -Wno-unused-but-set-variable -Wno-unused-parameter \
           -Wno-unused-variable -Wno-implicit-function-declaration \
           -Wno-format -Wno-incompatible-pointer-types -Wno-int-conversion \
           -D__embedded__

ROOT    := ../..
YAAGC   := $(ROOT)/third_party/virtualagc/yaAGC
AGC_CORE:= $(ROOT)/components/agc_core
ROM     := $(ROOT)/build/roms/Luminary099.bin

INC     := -I$(YAAGC) -I$(AGC_CORE)/include -I.

ENGINE_SRCS := \
    $(YAAGC)/agc_engine.c \
    $(YAAGC)/Backtrace.c \
    $(YAAGC)/DecodeDigitalDownlink.c

INIT_SRCS  := $(AGC_CORE)/agc_init.c
HOST_STUBS := test_io_stub.c

TESTS := test_rom_load test_engine_boot test_channel10_emit

.PHONY: all run clean $(addprefix run-, $(TESTS))
all: $(addsuffix .exe, $(TESTS))

%.exe: %.c $(ENGINE_SRCS) $(INIT_SRCS) $(HOST_STUBS)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm

run: $(addprefix run-, $(TESTS))
	@echo ALL PASS

run-%: %.exe
	@echo --- $* ---
	@ROM=$(ROM) ./$*.exe

clean:
	rm -f *.exe *.o

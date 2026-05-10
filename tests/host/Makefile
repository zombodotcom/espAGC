# Host tests for espAGC. Three layers:
#
#  Layer 1 (logic only) — test_rom_load / test_engine_boot /
#    test_channel10_emit / test_keypad_hit. Compile yaAGC engine +
#    our agc_init.c + a tiny io stub, no FreeRTOS, no channel_router.
#
#  Layer 2a (channel_router + engine in real wiring) — test_alarm_at_boot,
#    test_p00_select, test_lamp_test, test_replay_apollo11_launch. These
#    compile the *real* components/channel_router/channel_router.c
#    against tests/host/include/{freertos,esp_log}.h shims, plus
#    components/agc_core/io_callbacks.c.
#
#  Layer 2b (renderer pixel tests) — test_render_blank,
#    test_render_prog_lit. Compile dsky_render_320x240.c + font5x7.c +
#    dsky_layout.c + dsky_keypad_320x240.c.
#
# Run from this directory:
#   mingw32-make run     # build + run all test binaries
#   mingw32-make clean

CC      := gcc
CFLAGS  ?= -O2 -g -Wall -Wno-unused-but-set-variable -Wno-unused-parameter \
           -Wno-unused-variable -Wno-implicit-function-declaration \
           -Wno-format -Wno-incompatible-pointer-types -Wno-int-conversion \
           -D__embedded__

ROOT      := ../..
YAAGC     := $(ROOT)/third_party/virtualagc/yaAGC
AGC_CORE  := $(ROOT)/components/agc_core
DISP_HAL  := $(ROOT)/components/display_hal
CH_ROUTER := $(ROOT)/components/channel_router
PSTUB     := $(ROOT)/components/peripheral_stub
ROM       := $(ROOT)/build/roms/Luminary099.bin

INC     := -I$(YAAGC) -I$(AGC_CORE)/include -I.

ENGINE_SRCS := \
    $(YAAGC)/agc_engine.c \
    $(YAAGC)/Backtrace.c \
    $(YAAGC)/DecodeDigitalDownlink.c

INIT_SRCS    := $(AGC_CORE)/agc_init.c
HOST_STUBS   := test_io_stub.c

# Layer 2 build: real channel_router + io_callbacks compiled with shims.
HARNESS_INC  := -I$(YAAGC) -I$(AGC_CORE)/include -I$(CH_ROUTER)/include \
                -I$(PSTUB)/include -Iinclude -I.
HARNESS_SRCS := \
    $(ENGINE_SRCS) \
    $(INIT_SRCS) \
    $(AGC_CORE)/io_callbacks.c \
    $(CH_ROUTER)/channel_router.c \
    $(PSTUB)/peripheral_stub.c \
    agc_harness.c

LAYER1_TESTS := test_rom_load test_engine_boot test_channel10_emit
LAYER2_TESTS := test_alarm_at_boot test_p00_select test_lamp_test \
                test_rset_clears_alarms test_replay_apollo11_launch \
                test_auto_rset_at_boot test_peripheral_stub_clears_imodes \
                test_failreg_diagnostic
RENDER_TESTS := test_render_blank test_render_prog_lit

TESTS := $(LAYER1_TESTS) test_keypad_hit $(LAYER2_TESTS) $(RENDER_TESTS)

.PHONY: all run clean $(addprefix run-, $(TESTS))
all: $(addsuffix .exe, $(TESTS))

# --- Layer 1: engine + io stub --------------------------------------------
$(addsuffix .exe, $(LAYER1_TESTS)): %.exe: %.c $(ENGINE_SRCS) $(INIT_SRCS) $(HOST_STUBS)
	$(CC) $(CFLAGS) $(INC) -o $@ $^ -lm

# --- keypad hit-test: pure logic, no engine -------------------------------
test_keypad_hit.exe: test_keypad_hit.c $(DISP_HAL)/dsky_keypad_320x240.c
	$(CC) $(CFLAGS) \
	    -I$(DISP_HAL)/include \
	    -I$(CH_ROUTER)/include \
	    -I. \
	    -o $@ $^

# --- Layer 2a: real channel_router + harness ------------------------------
# test_replay only needs channel_router (no engine); but compiling it the
# same way as the rest is simpler and the binaries are still small.
$(addsuffix .exe, $(LAYER2_TESTS)): %.exe: %.c $(HARNESS_SRCS)
	$(CC) $(CFLAGS) $(HARNESS_INC) -o $@ $^ -lm

# --- Layer 2b: renderer pixel tests ---------------------------------------
RENDER_SRCS := \
    $(DISP_HAL)/dsky_render_320x240.c \
    $(DISP_HAL)/font5x7.c \
    $(DISP_HAL)/dsky_layout.c \
    $(DISP_HAL)/dsky_keypad_320x240.c

$(addsuffix .exe, $(RENDER_TESTS)): %.exe: %.c $(RENDER_SRCS)
	$(CC) $(CFLAGS) \
	    -I$(DISP_HAL)/include -I$(DISP_HAL) \
	    -I$(CH_ROUTER)/include -I. \
	    -o $@ $^

run: $(addprefix run-, $(TESTS))
	@echo ALL PASS

run-%: %.exe
	@echo --- $* ---
	@ROM=$(ROM) ./$*.exe

clean:
	rm -f *.exe *.o

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
                test_failreg_diagnostic test_lm_idle_quiet \
                test_executive_state test_charin_dispatch
LAYER2_FAILING_TESTS := test_p00_select_retry

# test_ref_compare needs CONFIG_AGC_LOG_ALL_OUTPUTS for raw-channel
# emission. Built separately with its own rule.
test_ref_compare.exe: test_ref_compare.c $(HARNESS_SRCS)
	$(CC) $(CFLAGS) -DCONFIG_AGC_AUTO_RSET_AT_BOOT=1 -DCONFIG_AGC_LOG_ALL_OUTPUTS=1 $(HARNESS_INC) -o $@ $^ -lm

# verify-ref: run our build through the same V35E + V37E00E sequence as
# ref_capture.py and assert no must-match channel emits values the
# reference doesn't. Real comparison test against captured ground truth.
.PHONY: verify-ref
verify-ref: test_ref_compare.exe
	@ROM=$(ROM) ./test_ref_compare.exe > local.log 2>&1 || true
	@bash verify_ref_match.sh local.log golden/ref_V35E_V37E00E.log
# yaagc_ref — vanilla yaAGC reference harness. Links upstream's
# agc_engine_init.c directly (not our agc_init.c) and NullAPI.c (no
# socket layer). Optional --sim flag interleaves peripheral_stub_step
# to test whether our LM_Simulator port changes vanilla behavior.
# Build:    mingw32-make yaagc_ref.exe
# Run:      ./yaagc_ref.exe                        # vanilla baseline
#           ./yaagc_ref.exe --sim                  # with peripheral_stub
yaagc_ref.exe: yaagc_ref.c $(YAAGC)/agc_engine.c $(YAAGC)/agc_engine_init.c \
               $(YAAGC)/Backtrace.c $(YAAGC)/DecodeDigitalDownlink.c \
               $(YAAGC)/NullAPI.c $(YAAGC)/rfopen.c \
               $(PSTUB)/peripheral_stub.c
	$(CC) $(CFLAGS) $(HARNESS_INC) -o $@ $^ -lm

# Diagnostic-only tests — build with the same rule as LAYER2 but not
# included in the default `run` target since they're investigation
# probes (long printouts, no assertions).
DIAG_TESTS   := test_z_histogram test_hotloop_disasm test_restart_path \
                test_keypress_response test_stuck_loop_state \
                test_delayed_rset_keypress test_find_keyrupt1 \
                test_slots_correct test_find_dsplock test_no_autorset_verb \
                test_slot0_origin test_slot0_code test_long_run \
                test_ch010_writes test_stuck_z_trace test_dump_exec_state \
                test_slot0_dwell test_dsptab_dump test_keypress_timeline test_verb_capture test_find_verbreg test_charin_state test_z_trace_after_verb test_cadrstor_when_charin test_charin_real_trace test_slot2_post_verb test_charin_verify test_check_restart test_long_verb test_charin_arrival test_slot0_evolve test_newjob_trace test_very_long test_accsokay_wait test_redoctr test_early_verb test_early_verb_trace test_v35e_full test_v37e00e_full test_v37_slots test_v37_trace test_v37_zhist test_v37_bankhist test_v37_trace2 test_v37_postcanv37 test_oktograb_caller test_oktograb_history test_v37_pooh_trace test_wakestal_sleeper
RENDER_TESTS := test_render_blank test_render_prog_lit

TESTS := $(LAYER1_TESTS) test_keypad_hit $(LAYER2_TESTS) $(RENDER_TESTS)

.PHONY: all diag run run-failing clean $(addprefix run-, $(TESTS)) $(addprefix run-, $(LAYER2_FAILING_TESTS))
all: $(addsuffix .exe, $(TESTS))
diag: $(addsuffix .exe, $(DIAG_TESTS))

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
# Host Layer-2 tests build with auto-RSET ON. On firmware the same flag
# lives in sdkconfig (set by Kconfig). channel_router.c uses `#ifdef`,
# so the symbol must be defined here to match Kconfig=y behavior.
$(addsuffix .exe, $(LAYER2_TESTS) $(LAYER2_FAILING_TESTS)): %.exe: %.c $(HARNESS_SRCS)
	$(CC) $(CFLAGS) -DCONFIG_AGC_AUTO_RSET_AT_BOOT=1 $(HARNESS_INC) -o $@ $^ -lm

# Diagnostic tests use the same build rule. Not in `run` target.
$(addsuffix .exe, $(DIAG_TESTS)): %.exe: %.c $(HARNESS_SRCS)
	$(CC) $(CFLAGS) -DCONFIG_AGC_AUTO_RSET_AT_BOOT=1 $(HARNESS_INC) -o $@ $^ -lm

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

# Real assertion tests known to FAIL right now. Keep separate from `run`
# so we don't lie about what works, but exposed via `run-failing` so it's
# easy to check the actual state. CI builds should run both and the
# failure mode is documented in tests/host/README.md.
run-failing: $(addsuffix .exe, $(LAYER2_FAILING_TESTS))
	@for t in $(LAYER2_FAILING_TESTS); do \
	    echo "--- $$t (expected to FAIL) ---"; \
	    ROM=$(ROM) ./$$t.exe || true; \
	done

run-%: %.exe
	@echo --- $* ---
	@ROM=$(ROM) ./$*.exe

clean:
	rm -f *.exe *.o

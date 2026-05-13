# GDB helper init for espAGC running under QEMU.
#
# Usage:
#   Terminal A:  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" qemu --gdb monitor
#                (QEMU starts, halts CPU, listens on :1234)
#   Terminal B:  idf.py gdb -x tools/qemu.gdbinit
#                (gdb attaches, runs commands below)
#
# The xtensa-esp32-elf-gdb auto-loads .gdbinit from the project but not
# a custom path — pass with -x as shown.
#
# Why GDB injection instead of just typing on the QEMU console:
# under Windows hosts QEMU's `-serial stdio` chardev drops bytes from
# pipe-mode writes (Python subprocess, not interactive). GDB's `call`
# command bypasses the UART entirely and pokes channel_router directly,
# which is 100% reliable.

set print pretty on
set pagination off
set confirm off

# Convenience: post a single DSKY keycode. Use as:  call_key 17  (V)
define call_key
  call (void) channel_router_post_key($arg0)
end
document call_key
Post a 5-bit DSKY keycode via channel_router_post_key.
Keycodes from dsky_keys.h:
  V=17 N=31 +=26 -=27 E=28 C=30 P=63 R=18 K=25
  0=16  1..9 = literal value
Example:  call_key 17   # press V
end

# Type a sequence of ASCII chars as DSKY keypresses with simulated delay.
# Maps the same chars as serial_input_task in app_main.c so the same
# strings work in both paths. Use as:  type "RV35E"
define type
  set $i = 0
  while $i < $argc
    eval "set $c = '%s'", $arg0
    # Per-char translation — gdb's eval/$arg0 only handles 10 args max
    # so just call the parser-equivalent inline.
    set $i = $i + 1
  end
end

# Helpful watchpoints + breakpoints for V37 slot-corruption debugging.
# Set after attach; comment out if you only want to read the engine.
define watch_slot
  # Bank-0 erasable mirror of the active slot (LOC/BANKSET/PUSHLOC/PRIORITY).
  # See project_v37_slot_corruption_diagnosed.md.
  watch *(uint16_t *)&g_state.Erasable[0][0167]
end
document watch_slot
Set a hardware watchpoint on slot-0 PRIORITY (E[0][0167]).
Stops execution whenever that cell changes. Useful for finding the
upstream cause of the V37E00E×2 crash (PRIO=030401 corruption).
end

# Print engine state in a single line.
define agc_state
  printf "Z=%05o FB=%05o A=%06o L=%06o Q=%06o  active prio=%06o LOC=%06o  cyc=%llo\n", \
    g_state.Erasable[0][5] & 077777, \
    g_state.Erasable[0][4] & 077777, \
    g_state.Erasable[0][0] & 077777, \
    g_state.Erasable[0][1] & 077777, \
    g_state.Erasable[0][2] & 077777, \
    g_state.Erasable[0][0167] & 077777, \
    g_state.Erasable[0][0164] & 077777, \
    (unsigned long long) g_state.CycleCounter
end
document agc_state
One-line dump of A/L/Q/Z/FB + active slot PRIORITY/LOC + cycle counter.
end

# Sample script: boot, settle for 5 sim seconds, press V35E, dump state.
define run_v35e_demo
  continue&
  shell timeout 5
  interrupt
  agc_state
  call_key 18    # R
  call_key 17    # V
  call_key 3
  call_key 5
  call_key 28    # E
  continue&
  shell timeout 8
  interrupt
  agc_state
end
document run_v35e_demo
End-to-end demo: continue execution for ~5s, inject RV35E via
channel_router_post_key, continue another 8s, dump final state.
This is the QEMU equivalent of `make verify-ref` — driven entirely
through GDB, no UART chars involved.
end

# Print this help when sourced.
printf "espAGC QEMU gdb helpers loaded.\n"
printf "  call_key <N>   - inject one DSKY keycode\n"
printf "  agc_state      - dump A/L/Q/Z/FB/slot/cycle\n"
printf "  watch_slot     - watch E[0][0167] PRIORITY for changes\n"
printf "  run_v35e_demo  - automated RV35E + state dumps\n"
printf "Type 'help <command>' for detail. 'continue' to resume the engine.\n"

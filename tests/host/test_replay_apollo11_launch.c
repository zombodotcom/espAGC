// test_replay_apollo11_launch — feed yaDSKY2's recorded Apollo 11 launch
// channel-output stream directly into our channel_router and snapshot the
// dsky_state at known checkpoints. This is the highest-fidelity Layer-2
// regression we can build without running the engine: yaAGC produced this
// output running the real ROM, so any divergence in our decode shows up
// as a wrong dsky_state.
//
// File format: lines of "<edge_ms_octal> <channel_octal> <value_octal>".
// Path: third_party/virtualagc/yaDSKY2/Apollo11-launch.canned
// Override with $REPLAY_FILE.

#include "channel_router.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *replay_path(void)
{
    const char *p = getenv("REPLAY_FILE");
    if (p && *p) return p;
    return "../../third_party/virtualagc/yaDSKY2/Apollo11-launch.canned";
}

static void print_snap(const char *tag, const dsky_state_t *s)
{
    #define DC(d) ((d) < 0 ? '_' : (char)('0' + (d)))
    printf("%-12s PRG=%c%c VRB=%c%c NUN=%c%c "
           "comp_acty=%d prog_alarm=%d\n",
           tag,
           DC(s->prog[0]), DC(s->prog[1]),
           DC(s->verb[0]), DC(s->verb[1]),
           DC(s->noun[0]), DC(s->noun[1]),
           s->comp_acty, s->prog_alarm);
    #undef DC
}

int main(void)
{
    const char *path = replay_path();
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "open %s failed\n", path); return 1; }

    channel_router_init();

    int line = 0, applied = 0;
    int max_lines = 5000;       // first ~5k events is enough to see PROG=02
    char buf[64];
    bool seen_prog02 = false;
    bool seen_comp_acty = false;

    dsky_state_t s;
    while (line < max_lines && fgets(buf, sizeof(buf), fp)) {
        line++;
        unsigned edge_o, ch_o, val_o;
        if (sscanf(buf, "%o %o %o", &edge_o, &ch_o, &val_o) != 3) continue;
        channel_router_on_output((int)ch_o, (int)val_o);
        applied++;

        if ((applied & 0x1FF) == 0) {
            channel_router_snapshot(&s);
            char tag[16];
            snprintf(tag, sizeof tag, "after %5d", applied);
            print_snap(tag, &s);
        }

        channel_router_snapshot(&s);
        if (s.prog[0] == 0 && s.prog[1] == 2) seen_prog02   = true;
        if (s.comp_acty)                       seen_comp_acty = true;
    }
    fclose(fp);

    channel_router_snapshot(&s);
    print_snap("FINAL", &s);

    ASSERT(applied > 100, "only applied %d events — file unreadable?", applied);
    ASSERT(seen_comp_acty,
           "COMP ACTY never lit — ch011 decoder broken?");
    // PROG=02 appears very early in the launch stream (P02, prelaunch
    // gyrocompassing). If this fails, the row-11 / digit-code decode is
    // off again.
    ASSERT(seen_prog02, "PROG never showed 02 — row-11 decode broken?");

    PASS();
}

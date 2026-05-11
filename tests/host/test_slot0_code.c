// test_slot0_code — slot 0's persistent auto-RSET CHARIN runs at
// LOC=02604..02607 in BANKSET=50001 (FBANK=20 octal = 16 decimal).
// Dump 30 instructions around that location to identify the routine.

#include "agc_harness.h"
#include "test_helpers.h"

#include "yaAGC.h"
#include "agc_engine.h"

#include <stdio.h>

extern agc_t *agc_core_state(void);
extern int agc_engine(agc_t *st);

int main(void)
{
    harness_boot();
    agc_t *st = agc_core_state();

    // BANKSET=50001 decoded:
    //   bits 0-2 (EBANK): 1
    //   top 5 bits (FBANK): 10100 = 0o24 = 20 decimal
    // But FBANK in BBANK format... yaAGC takes (BBANK >> 10) & 037.
    //   50001 >> 10 = 50  -> & 037 = 020 octal = 16 decimal
    // So FBANK = 16 decimal.

    int fbank_decimal = (050001 >> 10) & 037;
    int ebank = 050001 & 07;
    printf("BANKSET=050001 decodes: FBANK=%d (decimal), EBANK=%d\n",
           fbank_decimal, ebank);

    // LOC=02604..02607 — these are 12-bit addresses pointing into the
    // switched fixed memory. Switched memory is addresses 02000-03777.
    // With FBANK=16, switched memory resolves to Fixed[16][offset]
    // where offset = (Z - 02000) for Z in 02000..03777.

    int bank = fbank_decimal;
    printf("\n=== Fixed[%d] (FBANK %02o decimal %d) around offset 0600..0640 ===\n",
           bank, bank, bank);
    for (int off = 0600; off <= 0640; off++) {
        int w = st->Fixed[bank][off] & 077777;
        int op = (w >> 12) & 07;
        int z = off + 02000;
        const char *opname = "?";
        switch (op) {
            case 0: opname = "TC"; break;
            case 1: opname = "CCS/TCF"; break;
            case 2: opname = "DAS/LXCH/INCR/ADS"; break;
            case 3: opname = "CA"; break;
            case 4: opname = "CS"; break;
            case 5: opname = "INDEX/DXCH/TS/XCH"; break;
            case 6: opname = "AD"; break;
            case 7: opname = "MASK/MP/etc"; break;
        }
        char *mark = (off >= 0604 && off <= 0607) ? " <-- HOT" : "";
        printf("  Fixed[%d][%04o]=%05o  Z=%05o op=%d %-18s%s\n",
               bank, off, w, z, op, opname, mark);
    }

    // Also try the other FBANK we observed (BANKSET=10006 → FBANK=02).
    fbank_decimal = (010006 >> 10) & 037;
    printf("\n=== BANKSET=010006 → FBANK=%d ===\n", fbank_decimal);
    // LOC=20003-20007 with this BANKSET
    // 20003 >> 12 != 0, but switched-mem addresses are 02000-03777.
    // LOC=20003 is in the interpretive-PC encoding range, not raw mem.
    printf("LOC=20003 has top bit set — interpretive PC encoding, not raw addr.\n");

    PASS();
}

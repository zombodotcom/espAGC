// find_symbol — load Luminary099 symtab and print addresses for given names.
//   make find_symbol.exe
//   ./find_symbol.exe NOVAC FINDVAC CHANG2 MMCHANG REQEX1 VBPROC ENDOFJOB
#include <stdio.h>
#include <string.h>
#include "agc_symtab.h"

extern Symbol_t *SymbolTable;
extern int SymbolTableSize;

int main(int argc, char **argv) {
    char *path = "../../third_party/virtualagc/Luminary099/MAIN.agc.symtab";
    if (ReadSymbolTable(path) != 0) {
        fprintf(stderr, "failed to load %s\n", path);
        return 1;
    }
    for (int a = 1; a < argc; a++) {
        Symbol_t *s = ResolveSymbol(argv[a], SYMBOL_LABEL | SYMBOL_VARIABLE | SYMBOL_CONSTANT);
        if (!s) { printf("%-12s NOT FOUND\n", argv[a]); continue; }
        Address_t *v = &s->Value;
        if (v->Address && v->Fixed && v->Banked) {
            int sreg = v->SReg;
            printf("%-12s FB=%02o SBB=%d SREG=%05o (full=0%o)  [%s:%u]\n",
                   argv[a], v->FB, v->Super, sreg, v->Value,
                   s->FileName, s->LineNumber);
        } else if (v->Address && v->Erasable && v->Unbanked) {
            printf("%-12s EBANK=0 erasable=%05o\n", argv[a], v->SReg);
        } else if (v->Constant) {
            printf("%-12s CONST=%o\n", argv[a], v->Value);
        } else {
            printf("%-12s (other) full=%o\n", argv[a], v->Value);
        }
    }
    return 0;
}

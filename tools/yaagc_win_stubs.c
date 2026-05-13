// yaagc_win_stubs.c — minimal stubs replacing agc_symtab.c, agc_debugger.c,
// and agc_gdbmi.c on mingw. Those need POSIX regex + Linux signals we don't
// have. We never use --debug so all of these can fail silently.
#include <stddef.h>
#include <stdio.h>
#include "agc_symtab.h"
typedef struct agc_t agc_t;
typedef struct { int dummy; } Options_t_unused;

// agc_debugger.c stubs
void DbgDisplayVersion(void) { printf("yaAGC (Windows debug-less build)\n"); }
int  DbgInitialize(void *opts, void *state) { (void)opts; (void)state; return 0; }
int  DbgExecute(void) { return 0; }
void DbgMonitorBreakpoints(void *state) { (void)state; }
char *SymbolFile = NULL;

// agc_gdbmi.c stubs (only used via DbgExecute)
char *NormalizeSourceName(char *dir, char *fn) { (void)dir; (void)fn; return NULL; }
int  FindLastLineMain(void) { return 0; }
int  CheckDec(const char *s) { (void)s; return 0; }
SymbolLine_t *ResolveFileLineNumber(char *file, int line) { (void)file; (void)line; return NULL; }
SymbolLine_t *ResolveLineNumber(int line) { (void)line; return NULL; }
int  ListSourceLine(char *f, int n, char *c) { (void)f; (void)n; (void)c; return 0; }
int  NumberFiles = 0;
char **SourceFiles = NULL;
char *CurrentSourceFile = NULL;

Symbol_t *SymbolTable = NULL;
int       SymbolTableSize = 0;
SymbolLine_t *LineTable = NULL;
int           LineTableSize = 0;
char         *SourcePathName = NULL;

int  ReadSymbolTable(char *fname) { (void)fname; return 1; }
void ResetSymbolTable(void) {}
void WhatIsSymbol(char *name, int arch) { (void)name; (void)arch; }
void DumpSymbols(const char *pat, int arch) { (void)pat; (void)arch; }
void ListSource(char *file, int line) { (void)file; (void)line; }
void ListBackupSource(void) {}
void ListSourceRange(int from, int to) { (void)from; (void)to; }
Symbol_t *ResolveSymbol(char *name, int mask) { (void)name; (void)mask; return NULL; }
Symbol_t *ResolveSymbolAGC(int s, int fb, int sbb) { (void)s; (void)fb; (void)sbb; return NULL; }
SymbolLine_t *ResolveLineAGC(int z, int fb, int sbb) { (void)z; (void)fb; (void)sbb; return NULL; }
SymbolLine_t *ResolveFileLineAGC(char *f, int l) { (void)f; (void)l; return NULL; }
int  AddressPrintAGC(Address_t *a, char *s) { if (s) s[0]='\0'; return 0; }
int  AddressPrintAGS(Address_t *a, char *s) { if (s) s[0]='\0'; return 0; }

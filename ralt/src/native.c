/* native.c - P4 x86-64 backend. Emits AT&T assembly; system `as` assembles,
 * the C compiler is used only as a link driver. Structure per PLAN.md:
 * per-fn instruction lists -> linear-scan regalloc -> asm text.
 * WIP: this file is being built out in P4.1..P4.3 slices. */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>

void native_file(Arena *a, Decl **decls, int ndecls, const char *outpath, bool fast) {
    (void)a; (void)decls; (void)ndecls; (void)fast;
    FILE *out = fopen(outpath, "w");
    if (!out) {
        fprintf(stderr, "ralt: cannot write %s\n", outpath);
        exit(1);
    }
    fprintf(stderr, "ralt: native backend not implemented yet (P4 in flight)\n");
    fclose(out);
    exit(1);
}

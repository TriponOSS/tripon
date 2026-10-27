/* util.c - arena bump allocator + growable pointer vec */
#include "common.h"

static Arena *arena_extend(Arena *cur, size_t n) {
    Arena *a = calloc(1, sizeof *a);
    if (!a) { fputs("ralt: out of memory\n", stderr); exit(1); }
    a->cap = n > (1u << 20) ? n : (1u << 20);
    a->base = malloc(a->cap);
    if (!a->base) { fputs("ralt: out of memory\n", stderr); exit(1); }
    a->next = cur;
    return a;
}

Arena *arena_new(void) { return arena_extend(NULL, 0); }

void *arena_alloc(Arena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (a->used + n > a->cap) {
        Arena *fresh = arena_extend(NULL, n);
        /* splice: caller keeps head; we thread fresh behind cur so data lands in it */
        fresh->next = a->next;
        a->next = fresh;
        a = fresh;
    }
    void *p = a->base + a->used;
    a->used += n;
    return p;
}

void *arena_zalloc(Arena *a, size_t n) {
    void *p = arena_alloc(a, n);
    memset(p, 0, n);
    return p;
}

char *arena_dupstr(Arena *a, const char *s, size_t len) {
    char *p = arena_alloc(a, len + 1);
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

void vec_push_ptr(VecPtr *v, void *p) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = realloc(v->data, (size_t)v->cap * sizeof(void *));
        if (!v->data) { fputs("ralt: out of memory\n", stderr); exit(1); }
    }
    v->data[v->len++] = p;
}

/* ---------------- types ---------------- */

static const char *TY_NAMES[] = {
    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
    "isize", "usize", "f32", "f64", "bool", "str", "()",
    "struct", "enum", "typedef", "?", "opt", "result",
};
static const char *TY_CNAMES[] = {
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "intptr_t", "uintptr_t", "float", "double", "bool", "const char *", "void",
    "?struct", "?enum", "?typedef", "int32_t", "?opt", "?result",
};

const char *ty_name(Ty t) { return TY_NAMES[t]; }
const char *ty_cname(Ty t) { return TY_CNAMES[t]; }
bool ty_is_int(Ty t)   { return t >= TY_I8 && t <= TY_USIZE; }
bool ty_is_float(Ty t) { return t == TY_F32 || t == TY_F64; }
bool ty_is_num(Ty t)   { return ty_is_int(t) || ty_is_float(t); }

bool ty_is_bool(Ty t) { return t == TY_BOOL; }

/* ---------------- generic helpers ---------------- */

void diag_expected(Span sp, const char *what, const char *got) {
    char msg[200];
    snprintf(msg, sizeof msg, "expected %s, got '%s'", what, got ? got : "end of input");
    diag(sp, E0101, SEV_ERROR, msg, "see spec/grammar.ebnf for the legal shapes here");
}


/* ralt - the Tripon toolchain. C99, zero dependencies.
 * common.h: arena, spans, tokens, AST, diagnostics. One arena, no frees.
 */
#ifndef RALT_COMMON_H
#define RALT_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- arena ---------------- */

typedef struct Arena {
    char *base;
    size_t used, cap;
    struct Arena *next;
} Arena;

void *arena_alloc(Arena *a, size_t n);
void *arena_zalloc(Arena *a, size_t n);
char *arena_dupstr(Arena *a, const char *s, size_t len);
Arena *arena_new(void);

/* ---------------- growable arrays ---------------- */

typedef struct VecPtr {
    void **data;
    int len, cap;
} VecPtr;

void vec_push_ptr(VecPtr *v, void *p);

/* ---------------- spans + diagnostics ---------------- */

typedef struct {
    const char *file;
    int line, col, len;
} Span;

typedef enum { SEV_ERROR = 0, SEV_WARN, SEV_NOTE } Sev;

typedef struct {
    Span span;
    int code;       /* E#### from spec/errors.csv */
    Sev sev;
    char msg[240];
    char hint[200];
} Diag;

extern Diag *g_diags;
extern int g_ndiags, g_ncap;
extern int g_nerrors, g_nwarns;

void diag(Span sp, int code, Sev sev, const char *msg, const char *hint);
void print_diags(bool json);

bool is_c_reserved(const char *s);                   /* codegen.c */
void diag_expected(Span sp, const char *what, const char *got); /* util.c */

/* deps.c - ralt.yaml manifest + dependency collection */
int  deps_manifest_exists(void);
int  deps_init_template(void);
int  deps_install(const char *target);
int  deps_collect(char ***paths, int *count);
void deps_free(char ***paths, int count);

/* codes - keep in sync with spec/errors.csv; never renumber */
#define E0001 1
#define E0002 2
#define E0003 3
#define E0101 101
#define E0102 102
#define E0201 201
#define E0202 202
#define E0203 203
#define E0204 204
#define E0205 205
#define E0501 501
#define E0601 601
#define E0602 602
#define E0603 603

/* name/hint table mirrors spec/errors.csv - keep in sync (generated later) */
typedef struct {
    int code;
    Sev sev;
    const char *name;
    const char *hint;
} ErrEntry;
const ErrEntry *err_lookup(int code);

/* ---------------- tokens ---------------- */

typedef enum {
    TK_EOF = 0,
    TK_IDENT, TK_INT, TK_FLOAT, TK_STR,

    /* keywords */
    TK_FN, TK_LET, TK_CONST, TK_RETURN, TK_IF, TK_ELSE, TK_WHILE,
    TK_MATCH, TK_STRUCT, TK_ENUM, TK_USE, TK_TIER, TK_EXTERN,
    TK_TRUE, TK_FALSE, TK_MUT, TK_PUB,

    /* punctuation */
    TK_LP, TK_RP, TK_LB, TK_RB, TK_BRACE_L, TK_BRACE_R,
    TK_COMMA, TK_SEMI, TK_COLON, TK_COLON2, TK_DOT,
    TK_ARROW, TK_FATARROW,
    TK_ASSIGN, TK_ADDEQ, TK_SUBEQ, TK_MULEQ, TK_DIVEQ, TK_MODEQ,
    TK_EQ, TK_NE, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_AND, TK_OR, TK_NOT, TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
    TK_PERCENT, TK_AMP, TK_QUESTION, TK_POW,
} TokKind;

typedef struct {
    TokKind kind;
    Span span;
    const char *text;   /* ident/str bytes */
    size_t len;
    long long ival;     /* TK_INT */
    double fval;        /* TK_FLOAT */
} Token;

/* ---------------- types ---------------- */

typedef enum {
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64, TY_ISIZE, TY_USIZE,
    TY_F32, TY_F64, TY_BOOL, TY_STR, TY_VOID,
    TY_UNSET, /* pre-inference */
} Ty;

const char *ty_name(Ty t);
const char *ty_cname(Ty t);      /* C99 emit name */
bool ty_is_int(Ty t);
bool ty_is_float(Ty t);
bool ty_is_num(Ty t);

/* ---------------- AST ---------------- */

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct FnDecl FnDecl;
typedef struct Decl Decl;

typedef enum {
    EX_INT, EX_FLOAT, EX_STR, EX_BOOL, EX_IDENT,
    EX_BIN, EX_UN, EX_CALL, EX_INDEX,
} ExKind;

typedef enum { UN_NOT, UN_NEG, UN_ADDR } UnOp;

struct Expr {
    ExKind kind;
    Span span;
    Ty ty;
    bool lvalue, is_const_ref;

    long long ival;         /* EX_INT */
    double fval;            /* EX_FLOAT */
    const char *sval; size_t slen; /* EX_STR, EX_IDENT */
    bool bval;              /* EX_BOOL */

    TokKind op;             /* EX_BIN */
    Expr *lhs, *rhs;
    UnOp unop; Expr *operand; /* EX_UN */

    Expr **args; int nargs; /* EX_CALL */
    const char *callee;

    Expr *base, *index;     /* EX_INDEX */
};

typedef enum { ST_LET, ST_ASSIGN, ST_RETURN, ST_IF, ST_WHILE, ST_EXPR, ST_BLOCK } StKind;

struct Stmt {
    StKind kind;
    Span span;

    /* ST_LET */
    const char *name;
    Ty ty;
    bool is_const;
    Expr *init;

    /* ST_ASSIGN */
    Expr *target;   /* lvalue: EX_IDENT or EX_INDEX */
    Expr *value;
    TokKind aop;    /* the compound part or TK_ASSIGN */

    /* ST_RETURN / ST_IF / ST_WHILE */
    Expr *cond;
    Stmt **body; int nbody;  /* block children */
    Stmt *els;               /* ST_IF else-branch or NULL */
};

typedef struct {
    const char *name;
    Ty ty;
    Span span;
} Param;

struct FnDecl {
    const char *name;
    Param *params; int nparams;
    Ty ret;
    bool is_extern;
    Stmt **body; int nbody;
    Span span;
};

typedef enum { D_FN, D_TIER, D_USE } DeclKind;

struct Decl {
    DeclKind kind;
    FnDecl *fn;
    const char *name;   /* tier name */
    Span span;
};

/* ---------------- pipeline ---------------- */

const char *tok_kind_word(TokKind k);                /* lex.c */

/* typed vec struct aliases (VecPtr machinery reused, built on arena) */
typedef struct VecStmt { Stmt **data; int len, cap; } VecStmt;
typedef struct VecExpr { Expr **data; int len, cap; } VecExpr;
typedef struct VecDecl { Decl **data; int len, cap; } VecDecl;
typedef struct VecFn   { FnDecl **data; int len, cap; } VecFn;

static inline void vec_push_stmt(VecStmt *v, Arena *a, Stmt *s) {
    (void)a;
    void **slot;
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        /* realloc on heap; arena copies happen at parse exit */
    }
    slot = NULL;
    (void)slot;
    /* simple ptr-array growth (heap) — arenas own AST nodes, arrays own pointers */
    if (v->len == v->cap) {
        Stmt **na = realloc(v->data, (size_t)v->cap * sizeof(Stmt *)); /* cap already updated */
        if (!na) { fputs("ralt: out of memory\n", stderr); exit(1); }
        v->data = na;
    }
    v->data[v->len++] = s;
}

Token *lex_file(Arena *a, const char *src, size_t len, const char *file, int *ntoks);
Decl **parse_file(Arena *a, Token *toks, int ntoks, const char *file);
void typecheck_file(Arena *a, Decl **decls, int ndecls);
void codegen_file(Arena *a, Decl **decls, int ndecls, const char *outpath, bool fast);

void fmt_file(Arena *a, Decl **decls, int ndecls, FILE *out);            /* fmt.c */
void doc_file(Arena *a, Decl **decls, int ndecls, FILE *out, bool json); /* doc.c */
void lsp_serve(void);                                                    /* lsp.c */

#endif

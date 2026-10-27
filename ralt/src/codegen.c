/* codegen.c - Tripon -> C99. One pass, static output, no UB by contract.
 *   - uninit locals zero-init
 *   - int + - * -> trap (default) or wrap (-fast, two's complement)
 *   - int / % by zero -> trap in both modes; narrow-arith recheck traps in both
 *   - narrow ints: widen to i32, compute, range-recheck, narrow (all defined)
 *   - floats: IEEE semantics, no traps (documented in spec)
 * All guarded ops take (..., "file", line) so traps report real locations. */
#include "common.h"
#include <limits.h>

typedef struct {
    FILE *out;
    Arena *a;
    bool fast;
    FnDecl **funcs;
    int nfuncs;
    const char *file;
} Cg;

/* ---------------- helpers ---------------- */

bool is_c_reserved(const char *s) {
    static const char *RES[] = {
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","inline","int","long",
        "register","restrict","return","short","signed","sizeof","static","struct",
        "switch","typedef","union","unsigned","void","volatile","while",
        "_Alignas","_Alignof","_Atomic","_Bool","_Complex","_Generic",
        "_Imaginary","_Noreturn","_Static_assert","_Thread_local",
    };
    for (size_t i = 0; i < sizeof RES / sizeof RES[0]; i++)
        if (strcmp(RES[i], s) == 0) return true;
    return false;
}

static const char *cg_ident(Cg *g, const char *name) {
    size_t n = strlen(name);
    char *out = arena_alloc(g->a, n + 3);
    bool reserved = is_c_reserved(name) || (n > 1 && name[0] == '_' && name[1] == '_');
    size_t j = 0;
    if (reserved) out[j++] = 't';
    if (n > 0 && name[0] >= '0' && name[0] <= '9') out[j++] = 'x';
    memcpy(out + j, name, n);
    out[j + n] = 0;
    return out;
}

static const char *cg_fnname(Cg *g, FnDecl *fn) {
    if (strcmp(fn->name, "main") == 0) return "main";
    char *out = arena_alloc(g->a, strlen(fn->name) + 4);
    snprintf(out, (size_t)(strlen(fn->name) + 4), "tp_%s", fn->name);
    return out;
}

static void c_str_lit(Cg *g, const char *s, size_t len) {
    fputc('"', g->out);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '\n': fputs("\\n", g->out); break;
        case '\t': fputs("\\t", g->out); break;
        case '\r': fputs("\\r", g->out); break;
        case '\0': fputs("\\0", g->out); break;
        case '"':  fputs("\\\"", g->out); break;
        case '\\': fputs("\\\\", g->out); break;
        default:
            if (c < 0x20) fprintf(g->out, "\\%03o", c);
            else fputc(c, g->out);
        }
    }
    fputc('"', g->out);
}

/* ---------------- runtime prologue ---------------- */

static void emit_prologue(Cg *g) {
    fputs("#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n", g->out);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n", g->out);
    fputs("static void tp_fail(const char *s, int l, int c, const char *n) {\n", g->out);
    fputs("  fprintf(stderr, \"tripon trap E%04d %s at %s:%d\\n\", c, n, s, l);\n", g->out);
    fputs("  exit(101);\n}\n", g->out);

    if (!g->fast) {
        fputs("static int32_t tp_add32_g(int32_t a, int32_t b, const char *s, int l) { int32_t r; if (__builtin_add_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static int32_t tp_sub32_g(int32_t a, int32_t b, const char *s, int l) { int32_t r; if (__builtin_sub_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static int32_t tp_mul32_g(int32_t a, int32_t b, const char *s, int l) { int32_t r; if (__builtin_mul_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static int64_t tp_add64_g(int64_t a, int64_t b, const char *s, int l) { int64_t r; if (__builtin_add_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static int64_t tp_sub64_g(int64_t a, int64_t b, const char *s, int l) { int64_t r; if (__builtin_sub_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static int64_t tp_mul64_g(int64_t a, int64_t b, const char *s, int l) { int64_t r; if (__builtin_mul_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint32_t tp_addu32_g(uint32_t a, uint32_t b, const char *s, int l) { uint32_t r; if (__builtin_add_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint32_t tp_subu32_g(uint32_t a, uint32_t b, const char *s, int l) { uint32_t r; if (__builtin_sub_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint32_t tp_mulu32_g(uint32_t a, uint32_t b, const char *s, int l) { uint32_t r; if (__builtin_mul_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint64_t tp_addu64_g(uint64_t a, uint64_t b, const char *s, int l) { uint64_t r; if (__builtin_add_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint64_t tp_subu64_g(uint64_t a, uint64_t b, const char *s, int l) { uint64_t r; if (__builtin_sub_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
        fputs("static uint64_t tp_mulu64_g(uint64_t a, uint64_t b, const char *s, int l) { uint64_t r; if (__builtin_mul_overflow(a, b, &r)) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return r; }\n", g->out);
    }
    fputs("static int32_t tp_divz_g(int32_t a, int32_t b, const char *s, int l) { if (b == 0) tp_fail(s, l, 9003, \"division-by-zero-trap\"); return a / b; }\n", g->out);
    fputs("static int32_t tp_modz_g(int32_t a, int32_t b, const char *s, int l) { if (b == 0) tp_fail(s, l, 9003, \"division-by-zero-trap\"); return a % b; }\n", g->out);
    fputs("static int64_t tp_divz64_g(int64_t a, int64_t b, const char *s, int l) { if (b == 0) tp_fail(s, l, 9003, \"division-by-zero-trap\"); return a / b; }\n", g->out);
    fputs("static int64_t tp_modz64_g(int64_t a, int64_t b, const char *s, int l) { if (b == 0) tp_fail(s, l, 9003, \"division-by-zero-trap\"); return a % b; }\n", g->out);
    fputs("static int8_t  tp_nar8_g(int32_t v, const char *s, int l)  { if (v < INT8_MIN   || v > INT8_MAX)   tp_fail(s, l, 9001, \"integer-overflow-trap\"); return (int8_t)v; }\n", g->out);
    fputs("static int16_t tp_nar16_g(int32_t v, const char *s, int l) { if (v < INT16_MIN  || v > INT16_MAX)  tp_fail(s, l, 9001, \"integer-overflow-trap\"); return (int16_t)v; }\n", g->out);
    fputs("static uint8_t  tp_naru8_g(int32_t v, const char *s, int l)  { if (v < 0 || v > UINT8_MAX)  tp_fail(s, l, 9001, \"integer-overflow-trap\"); return (uint8_t)v; }\n", g->out);
    fputs("static uint16_t tp_naru16_g(int32_t v, const char *s, int l) { if (v < 0 || v > UINT16_MAX) tp_fail(s, l, 9001, \"integer-overflow-trap\"); return (uint16_t)v; }\n", g->out);

    fputs("static void tp_builtin_print_i32(int32_t x) { printf(\"%d\\n\", x); }\n", g->out);
    fputs("static void tp_builtin_print_i64(int64_t x) { printf(\"%lld\\n\", (long long)x); }\n", g->out);
    fputs("static void tp_builtin_print_f64(double x) { printf(\"%.2f\\n\", x); }\n", g->out);
    fputs("static void tp_builtin_print_str(const char *x) { fputs(x, stdout); fputc('\\n', stdout); }\n", g->out);
    fputs("static int32_t tp_builtin_len(const char *x) { return (int32_t)strlen(x); }\n", g->out);
    fputs("static int tp_str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }\n", g->out);
    fputs("static double tp_pow_f64(double a, double b) { return pow(a, b); }\n", g->out);
}

/* ---------------- expression emission ---------------- */

static void emit_expr(Cg *g, Expr *e);

static const char *ovf_helper(Ty t, TokKind op) {
    switch (t) {
    case TY_I32: return op == TK_PLUS ? "tp_add32_g" : op == TK_MINUS ? "tp_sub32_g" : "tp_mul32_g";
    case TY_I64: return op == TK_PLUS ? "tp_add64_g" : op == TK_MINUS ? "tp_sub64_g" : "tp_mul64_g";
    case TY_U32: return op == TK_PLUS ? "tp_addu32_g" : op == TK_MINUS ? "tp_subu32_g" : "tp_mulu32_g";
    case TY_U64: return op == TK_PLUS ? "tp_addu64_g" : op == TK_MINUS ? "tp_subu64_g" : "tp_mulu64_g";
    default: return NULL;
    }
}

static const char *div_helper(Ty t, bool is_mod) {
    switch (t) {
    case TY_I64: case TY_U64: return is_mod ? "tp_modz64_g" : "tp_divz64_g";
    default: return is_mod ? "tp_modz_g" : "tp_divz_g";
    }
}

static const char *narrow_helper(Ty t) {
    switch (t) {
    case TY_I8: return "tp_nar8_g";
    case TY_I16: return "tp_nar16_g";
    case TY_U8: return "tp_naru8_g";
    case TY_U16: return "tp_naru16_g";
    default: return NULL;
    }
}

static const char *sym_word(TokKind k) {
    switch (k) {
    case TK_PLUS: return " + ";
    case TK_MINUS: return " - ";
    case TK_STAR: return " * ";
    case TK_SLASH: return " / ";
    case TK_PERCENT: return " % ";
    default: return " <?> ";
    }
}

static void cg_trap_args(Cg *g, Span sp) {
    fprintf(g->out, ", \"%s\", %d", g->file, sp.line);
}

static void emit_bin(Cg *g, Expr *e) {
    Ty t = e->lhs->ty == TY_UNSET ? e->rhs->ty : e->lhs->ty;
    bool is_int = ty_is_int(t);
    bool is_float = ty_is_float(t);

    switch (e->op) {
    case TK_EQ:
    case TK_NE: {
        if (e->lhs->ty == TY_STR) {
            if (e->op == TK_EQ) fputs("tp_str_eq(", g->out);
            else fputs("(!tp_str_eq(", g->out);
            emit_expr(g, e->lhs);
            fputs(", ", g->out);
            emit_expr(g, e->rhs);
            fputs(e->op == TK_EQ ? ")" : "))", g->out);
        } else {
            emit_expr(g, e->lhs);
            fputs(e->op == TK_EQ ? " == " : " != ", g->out);
            emit_expr(g, e->rhs);
        }
        break;
    }
    case TK_AND:
        emit_expr(g, e->lhs); fputs(" && ", g->out); emit_expr(g, e->rhs);
        break;
    case TK_OR:
        emit_expr(g, e->lhs); fputs(" || ", g->out); emit_expr(g, e->rhs);
        break;
    case TK_LT: case TK_GT: case TK_LE: case TK_GE: {
        emit_expr(g, e->lhs);
        if (e->op == TK_LT) fputs(" < ", g->out);
        else if (e->op == TK_GT) fputs(" > ", g->out);
        else if (e->op == TK_LE) fputs(" <= ", g->out);
        else fputs(" >= ", g->out);
        emit_expr(g, e->rhs);
        break;
    }
    case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT: {
        bool is_div = e->op == TK_SLASH || e->op == TK_PERCENT;

        /* narrow ints: widen -> guarded/plain i32 math -> narrow recheck */
        if (is_int && narrow_helper(t)) {
            fputs("(", g->out);
            fputs(narrow_helper(t), g->out);
            fputs("((", g->out);
            if (is_div) {
                fprintf(g->out, "%s((int32_t)", div_helper(TY_I32, e->op == TK_PERCENT));
                emit_expr(g, e->lhs);
                cg_trap_args(g, e->span);
                fputs(", (int32_t)", g->out);
                emit_expr(g, e->rhs);
                fputs(")", g->out);
            } else if (g->fast) {
                fputs("(int32_t)", g->out);
                emit_expr(g, e->lhs);
                fprintf(g->out, "%s(int32_t)", sym_word(e->op));
                emit_expr(g, e->rhs);
            } else {
                fprintf(g->out, "%s((int32_t)", ovf_helper(TY_I32, e->op));
                emit_expr(g, e->lhs);
                fputs(", (int32_t)", g->out);
                emit_expr(g, e->rhs);
                fputs(")", g->out);
            }
            fputs("))", g->out);   /* close: narrow( ..., ... ) then the outer ( */
            cg_trap_args(g, e->span);
            fputs(")", g->out);    /* close the narrow recheck call itself */
            break;
        }

        /* wide ints: guarded helpers (default), plain math for -fast, div always trapped */
        if (is_int) {
            const char *h =
                is_div ? div_helper(t, e->op == TK_PERCENT)
                       : (g->fast ? NULL : ovf_helper(t, e->op));
            if (h) {
                fprintf(g->out, "%s(", h);
                emit_expr(g, e->lhs);
                fputs(", ", g->out);
                emit_expr(g, e->rhs);
                cg_trap_args(g, e->span);
                fputs(")", g->out);
                break;
            }
        }

        /* floats always, wide ints under -fast: plain C */
        fputs("(", g->out);
        emit_expr(g, e->lhs);
        fputs(sym_word(e->op), g->out);
        emit_expr(g, e->rhs);
        fputs(")", g->out);
        break;
    }
    case TK_POW: {
        fputs("tp_pow_f64(", g->out);
        emit_expr(g, e->lhs);
        fputs(", ", g->out);
        emit_expr(g, e->rhs);
        fputs(")", g->out);
        break;
    }
    default: break;
    }
    (void)is_float;
}

static bool is_builtin(const char *name) {
    static const char *B[] = {"print_i32", "print_i64", "print_f64", "print_str", "len"};
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++)
        if (strcmp(B[i], name) == 0) return true;
    return false;
}

static void emit_expr(Cg *g, Expr *e) {
    switch (e->kind) {
    case EX_INT:
        fprintf(g->out, "%lld%s", (long long)e->ival, e->ty == TY_I64 ? "LL" : "");
        break;
    case EX_FLOAT:
        fprintf(g->out, "%.17g", e->fval);
        break;
    case EX_STR:
        c_str_lit(g, e->sval, e->slen);
        break;
    case EX_BOOL:
        fputs(e->bval ? "true" : "false", g->out);
        break;
    case EX_IDENT:
        fputs(cg_ident(g, e->sval), g->out);
        break;
    case EX_CALL: {
        if (is_builtin(e->callee)) {
            fprintf(g->out, "tp_builtin_%s(", e->callee);
            emit_expr(g, e->args[0]);
            fputs(")", g->out);
            break;
        }
        FnDecl *fn = NULL;
        for (int i = 0; i < g->nfuncs; i++)
            if (strcmp(g->funcs[i]->name, e->callee) == 0) { fn = g->funcs[i]; break; }
        if (fn) fprintf(g->out, "%s(", fn->is_extern ? fn->name : cg_fnname(g, fn));
        else fprintf(g->out, "%s(", e->callee);
        for (int i = 0; i < e->nargs; i++) {
            if (i) fputs(", ", g->out);
            emit_expr(g, e->args[i]);
        }
        fputs(")", g->out);
        break;
    }
    case EX_UN:
        if (e->unop == UN_NOT) {
            fputs("(!", g->out);
            emit_expr(g, e->operand);
            fputs(")", g->out);
        } else {
            fputs("-", g->out);
            emit_expr(g, e->operand);
        }
        break;
    case EX_BIN:
        emit_bin(g, e);
        break;
    default:
        break;
    }
}

/* ---------------- statements ---------------- */

static void emit_stmt(Cg *g, Stmt *s) {
    switch (s->kind) {
    case ST_LET: {
        bool hasinit = s->init != NULL;
        if (!hasinit) {
            const char *zero = "0";
            switch (s->ty) {
            case TY_F32: zero = "0.0f"; break;
            case TY_F64: zero = "0.0"; break;
            case TY_BOOL: zero = "false"; break;
            case TY_STR: zero = "\"\""; break;
            default: zero = "0"; break;
            }
            fprintf(g->out, "%s %s = %s;\n", ty_cname(s->ty), cg_ident(g, s->name), zero);
        } else {
            fprintf(g->out, "%s %s = ", ty_cname(s->ty), cg_ident(g, s->name));
            emit_expr(g, s->init);
            fputs(";\n", g->out);
        }
        break;
    }
    case ST_ASSIGN: {
        Expr *t = s->target;
        const char *ident = cg_ident(g, t->kind == EX_IDENT ? t->sval : "?");
        if (s->aop == TK_ASSIGN) {
            fprintf(g->out, "%s = ", ident);
            emit_expr(g, s->value);
            fputs(";\n", g->out);
            break;
        }
        /* compound assignment lowers through the same trap-backed binary emit */
        Expr fake;
        memset(&fake, 0, sizeof fake);
        fake.kind = EX_BIN;
        fake.span = s->span;
        fake.ty = t->ty;
        fake.lhs = t;
        fake.rhs = s->value;
        switch (s->aop) {
        case TK_ADDEQ: fake.op = TK_PLUS; break;
        case TK_SUBEQ: fake.op = TK_MINUS; break;
        case TK_MULEQ: fake.op = TK_STAR; break;
        case TK_DIVEQ: fake.op = TK_SLASH; break;
        default: fake.op = TK_PERCENT; break;
        }
        fprintf(g->out, "%s = ", ident);
        emit_expr(g, &fake);
        fputs(";\n", g->out);
        break;
    }
    case ST_RETURN:
        if (s->value) {
            fputs("return ", g->out);
            emit_expr(g, s->value);
        } else {
            fputs("return", g->out);
        }
        fputs(";\n", g->out);
        break;
    case ST_IF:
        fputs("if (", g->out);
        emit_expr(g, s->cond);
        fputs(") {\n", g->out);
        for (int i = 0; i < s->nbody; i++) emit_stmt(g, s->body[i]);
        if (s->els) {
            fputs("} else ", g->out);
            if (s->els->kind == ST_IF) {
                fputs("if (", g->out);
                emit_expr(g, s->els->cond);
                fputs(") {\n", g->out);
                for (int i = 0; i < s->els->nbody; i++) emit_stmt(g, s->els->body[i]);
                fputs("}\n", g->out);
            } else {
                fputs("{\n", g->out);
                for (int i = 0; i < s->els->nbody; i++) emit_stmt(g, s->els->body[i]);
                fputs("}\n", g->out);
            }
        } else {
            fputs("}\n", g->out);
        }
        break;
    case ST_WHILE:
        fputs("while (", g->out);
        emit_expr(g, s->cond);
        fputs(") {\n", g->out);
        for (int i = 0; i < s->nbody; i++) emit_stmt(g, s->body[i]);
        fputs("}\n", g->out);
        break;
    case ST_EXPR:
        emit_expr(g, s->value);
        fputs(";\n", g->out);
        break;
    case ST_BLOCK:
        fputs("{\n", g->out);
        for (int i = 0; i < s->nbody; i++) emit_stmt(g, s->body[i]);
        fputs("}\n", g->out);
        break;
    default:
        break;
    }
}

/* ---------------- functions ---------------- */

static void emit_fn_proto(Cg *g, FnDecl *fn) {
    bool is_main = strcmp(fn->name, "main") == 0;
    if (is_main) {
        fputs("int main(void);\n", g->out);
        return;
    }
    fprintf(g->out, "static %s %s(", ty_cname(fn->ret), cg_fnname(g, fn));
    if (fn->nparams == 0) fputs("void", g->out);
    for (int i = 0; i < fn->nparams; i++) {
        if (i) fputs(", ", g->out);
        fprintf(g->out, "%s %s", ty_cname(fn->params[i].ty), cg_ident(g, fn->params[i].name));
    }
    fputs(");\n", g->out);
}

static void emit_fn(Cg *g, FnDecl *fn) {
    if (fn->is_extern) {
        fprintf(g->out, "extern %s %s(", ty_cname(fn->ret), fn->name);
        for (int i = 0; i < fn->nparams; i++) {
            if (i) fputs(", ", g->out);
            fputs(ty_cname(fn->params[i].ty), g->out);
        }
        fputs(");\n", g->out);
        return;
    }
    bool is_main = strcmp(fn->name, "main") == 0;
    if (is_main) {
        fprintf(g->out, "int main(void) {\n");
    } else {
        fprintf(g->out, "static %s %s(", ty_cname(fn->ret), cg_fnname(g, fn));
        if (fn->nparams == 0) fputs("void", g->out);
        for (int i = 0; i < fn->nparams; i++) {
            if (i) fputs(", ", g->out);
            fprintf(g->out, "%s %s", ty_cname(fn->params[i].ty), cg_ident(g, fn->params[i].name));
        }
        fputs(") {\n", g->out);
    }
    for (int i = 0; i < fn->nbody; i++) emit_stmt(g, fn->body[i]);
    /* defined behavior: nothing falls off the end */
    bool last_is_ret = fn->nbody > 0 && fn->body[fn->nbody - 1]->kind == ST_RETURN;
    if (!last_is_ret && (is_main || fn->ret != TY_VOID)) {
        if (fn->ret == TY_F32) fputs("return 0.0f;\n", g->out);
        else if (fn->ret == TY_F64) fputs("return 0.0;\n", g->out);
        else if (fn->ret == TY_BOOL) fputs("return false;\n", g->out);
        else if (fn->ret == TY_STR) fputs("return \"\";\n", g->out);
        else fprintf(g->out, "return 0;\n");
    }
    if (is_main && fn->ret == TY_VOID) fputs("return 0;\n", g->out);
    fputs("}\n", g->out);
}

void codegen_file(Arena *a, Decl **decls, int ndecls, const char *outpath, bool fast) {
    FILE *out = fopen(outpath, "w");
    if (!out) {
        fprintf(stderr, "ralt: cannot write %s\n", outpath);
        exit(1);
    }
    Cg g;
    memset(&g, 0, sizeof g);
    g.out = out;
    g.a = a;
    g.fast = fast;
    g.funcs = arena_alloc(a, sizeof(FnDecl *) * (size_t)(ndecls ? ndecls : 1));
    for (int i = 0; i < ndecls; i++)
        if (decls[i]->kind == D_FN) g.funcs[g.nfuncs++] = decls[i]->fn;
    if (ndecls && decls[0]->span.file) g.file = decls[0]->span.file;

    emit_prologue(&g);
    fputs("\n/* ---- extern blocks ---- */\n", out);
    for (int i = 0; i < ndecls; i++)
        if (decls[i]->kind == D_FN && decls[i]->fn->is_extern) emit_fn(&g, decls[i]->fn);
    /* forward declarations: Tripon allows forward/mutual refs, deps merge freely */
    fputs("\n/* ---- forward decls ---- */\n", out);
    for (int i = 0; i < ndecls; i++)
        if (decls[i]->kind == D_FN && !decls[i]->fn->is_extern) emit_fn_proto(&g, decls[i]->fn);
    fputs("\n/* ---- tripon fns (main maps to C main) ---- */\n", out);
    for (int i = 0; i < ndecls; i++)
        if (decls[i]->kind == D_FN && !decls[i]->fn->is_extern) emit_fn(&g, decls[i]->fn);
    fclose(out);
}

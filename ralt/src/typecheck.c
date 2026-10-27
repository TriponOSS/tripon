/* typecheck.c - the type-safety contract enforcement (spec section 3).
 * Strict: no implicit conversions of any kind, no null paths, errors as values. */
#include "common.h"

/* ---------------- symbol tables ---------------- */

typedef struct {
    const char *name;
    Ty ty;
    bool is_const;
    Span span;
} Local;

typedef struct Scope {
    Local *locals;
    int nlocals;
    struct Scope *parent;
} Scope;

typedef struct {
    Arena *a;
    Scope *scopes;
    FnDecl **funcs;
    int nfuncs;
    FnDecl *cur_fn;
} Ctx;

typedef enum { FOUND = 0, NOTFOUND } Res;

static Res scope_insert(Ctx *c, const char *name, Ty ty, bool is_const, Span sp) {
    if (!c->scopes) {
        c->scopes = (Scope *)arena_zalloc(c->a, sizeof(Scope));
        c->scopes->parent = NULL;
    }
    /* shadowing check across all open scopes (warn only, later: config) */
    for (Scope *f = c->scopes; f; f = f->parent)
        for (int i = 0; i < f->nlocals; i++)
            if (strcmp(f->locals[i].name, name) == 0) {
                char msg[200];
                snprintf(msg, sizeof msg, "'%s' shadows an existing binding", name);
                diag(sp, E0603, SEV_WARN, msg, "rename the inner binding or drop shadowing intent");
                goto done;
            }
done: ;
    Local *na = arena_alloc(c->a, sizeof(Local) * (size_t)(c->scopes->nlocals + 1));
    if (c->scopes->nlocals)
        memcpy(na, c->scopes->locals, sizeof(Local) * (size_t)c->scopes->nlocals);
    na[c->scopes->nlocals] = (Local){name, ty, is_const, sp};
    c->scopes->locals = na;
    c->scopes->nlocals++;
    return FOUND;
}

/* ---------------- built-in stdlib prelude (tier core) ---------------- */

typedef struct {
    const char *name;
    Ty ret;
    Ty arg;
} Builtin;

static const Builtin BUILTINS[] = {
    /* Tripon stdlib prelude v0, tier core, always available, no import */
    {"print_i32", TY_VOID, TY_I32},
    {"print_i64", TY_VOID, TY_I64},
    {"print_f64", TY_VOID, TY_F64},
    {"print_str", TY_VOID, TY_STR},
    {"len",       TY_I32, TY_STR},
};

static bool builtin_info(const char *name, int *ret, int *argc, Ty *argty) {
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) {
            *ret = BUILTINS[i].ret;
            *argc = 1;
            *argty = BUILTINS[i].arg;
            return true;
        }
    }
    return false;
}

/* ---------------- fn table prepass ---------------- */

static void prepass_fns(Ctx *c, Decl **decls, int ndecls) {
    c->funcs = arena_zalloc(c->a, sizeof(FnDecl *) * (size_t)(ndecls ? ndecls : 1));
    for (int i = 0; i < ndecls; i++) {
        if (decls[i]->kind == D_FN) {
            /* duplicate check */
            for (int j = 0; j < i && j < c->nfuncs; j++)
                if (strcmp(c->funcs[j]->name, decls[i]->fn->name) == 0 && !decls[i]->fn->is_extern) {
                    char msg[180];
                    snprintf(msg, sizeof msg, "function '%s' is already defined", decls[i]->fn->name);
                    diag(decls[i]->fn->span, E0201, SEV_ERROR, msg,
                         "one definition per name in a compilation unit");
                }
            c->funcs[c->nfuncs++] = decls[i]->fn;
        }
    }
}

static FnDecl *find_fn(Ctx *c, const char *name) {
    for (int i = 0; i < c->nfuncs; i++)
        if (strcmp(c->funcs[i]->name, name) == 0) return c->funcs[i];
    return NULL;
}

/* ---------------- expression typing ---------------- */

static Ty tc_expr(Ctx *c, Expr *e) {
    switch (e->kind) {
    case EX_INT: {
        /* literal type: fits i32 -> i32, fits i64 -> i64 */
        e->ty = (e->ival >= INT32_MIN && e->ival <= INT32_MAX) ? TY_I32 : TY_I64;
        return e->ty;
    }
    case EX_FLOAT: e->ty = TY_F64; return TY_F64;
    case EX_STR:  e->ty = TY_STR;  return TY_STR;
    case EX_BOOL: e->ty = TY_BOOL; return TY_BOOL;

    case EX_IDENT: {
        for (Scope *f = c->scopes; f; f = f->parent)
            for (int i = 0; i < f->nlocals; i++)
                if (strcmp(f->locals[i].name, e->sval) == 0) {
                    e->ty = f->locals[i].ty;
                    e->lvalue = true;
                    e->is_const_ref = f->locals[i].is_const;
                    return e->ty;
                }
        char msg[200];
        snprintf(msg, sizeof msg, "unknown identifier '%s' in this scope", e->sval);
        diag(e->span, E0201, SEV_ERROR, msg, "define it, use the right module, or fix spelling");
        e->ty = TY_UNSET;
        return TY_UNSET;
    }

    case EX_CALL: {
        int bret, bargc; Ty barg;
        if (builtin_info(e->callee, &bret, &bargc, &barg)) {
            if (e->nargs != bargc) {
                char msg[200];
                snprintf(msg, sizeof msg, "builtin '%s' takes %d argument(s), got %d",
                         e->callee, bargc, e->nargs);
                diag(e->span, E0202, SEV_ERROR, msg, "match the signature exactly");
            } else {
                Ty at = tc_expr(c, e->args[0]);
                /* no cascades: a deeper-reported arg stops here */
                if (at != TY_UNSET && at != barg) {
                    char msg[240];
                    snprintf(msg, sizeof msg, "builtin '%s' expects %s, got %s",
                             e->callee, ty_name(barg), ty_name(at));
                    diag(e->args[0]->span, E0202, SEV_ERROR, msg,
                         "no implicit conversion exists in Tripon");
                }
            }
            e->ty = (Ty)bret;
            return e->ty;
        }
        FnDecl *fn = find_fn(c, e->callee);
        if (!fn) {
            char msg[200];
            snprintf(msg, sizeof msg, "call to unknown function '%s'", e->callee);
            diag(e->span, E0201, SEV_ERROR, msg, "declare it at top level or in an extern block");
            e->ty = TY_UNSET;
            return TY_UNSET;
        }
        if (e->nargs != fn->nparams) {
            char msg[220];
            snprintf(msg, sizeof msg, "'%s' takes %d argument(s), got %d",
                     fn->name, fn->nparams, e->nargs);
            diag(e->span, E0202, SEV_ERROR, msg, "count the parameters in the signature");
            for (int i = 0; i < e->nargs; i++) tc_expr(c, e->args[i]);
            e->ty = fn->ret;
            return e->ty;
        }
        for (int i = 0; i < e->nargs; i++) {
            Ty at = tc_expr(c, e->args[i]);
            /* no cascades: if the argument already errored deeper, stop here */
            if (at == TY_UNSET || fn->params[i].ty == TY_UNSET) continue;
            if (at != fn->params[i].ty) {
                char msg[260];
                snprintf(msg, sizeof msg, "argument %d of '%s' must be %s, got %s",
                         i + 1, fn->name,
                         ty_name(fn->params[i].ty), ty_name(at));
                diag(e->args[i]->span, E0202, SEV_ERROR, msg,
                     "no implicit conversion exists in Tripon; cast<T> or declare the exact type");
            }
        }
        e->ty = fn->ret;
        return e->ty;
    }

    case EX_UN:
        if (e->unop == UN_NOT) {
            Ty t = tc_expr(c, e->operand);
            if (t != TY_BOOL) {
                diag(e->operand->span, E0202, SEV_ERROR, "'!' requires bool",
                     "comparisons produce bool; there is no truthiness in Tripon");
            }
            e->ty = TY_BOOL;
            return TY_BOOL;
        }
        if (e->unop == UN_NEG) {
            Ty t = tc_expr(c, e->operand);
            if (!ty_is_num(t)) {
                diag(e->operand->span, E0202, SEV_ERROR, "'-' requires a number",
                     "arith ops exist only on numeric types");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            /* negating unsigned is rejected outright */
            if (ty_is_int(t) && t >= TY_U8 && t <= TY_USIZE) {
                diag(e->operand->span, E0202, SEV_ERROR, "cannot negate an unsigned value",
                     "wrapping-neg of unsigned is not a thing; use a signed type");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            e->ty = t;
            return t;
        }
        e->ty = TY_UNSET;
        return TY_UNSET;

    case EX_BIN: {
        Ty lhs = tc_expr(c, e->lhs);
        Ty rhs = tc_expr(c, e->rhs);
        int is_cmp = e->op == TK_LT || e->op == TK_GT || e->op == TK_LE || e->op == TK_GE;
        int is_eq = e->op == TK_EQ || e->op == TK_NE;
        int is_log = e->op == TK_AND || e->op == TK_OR;
        int is_arith = e->op == TK_PLUS || e->op == TK_MINUS ||
                       e->op == TK_STAR || e->op == TK_SLASH || e->op == TK_PERCENT ||
                       e->op == TK_POW;

        if (is_log) {
            if (lhs != TY_BOOL || rhs != TY_BOOL) {
                diag(e->span, E0202, SEV_ERROR, "logical ops require bool operands on both sides",
                     "there is no truthiness in Tripon; compare explicitly");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            e->ty = TY_BOOL;
            return TY_BOOL;
        }

        if (lhs == TY_UNSET || rhs == TY_UNSET) {
            e->ty = TY_UNSET;
            return TY_UNSET;/* error already reported deeper */
        }

        if (is_cmp || is_eq) {
            if (lhs != rhs) {
                char msg[240];
                snprintf(msg, sizeof msg, "cannot compare %s with %s; no implicit conversion exists",
                         ty_name(lhs), ty_name(rhs));
                diag(e->span, E0202, SEV_ERROR, msg,
                     "same-type comparisons only; cast one side explicitly if that is the intent");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            if (is_eq && !ty_is_num(lhs) && lhs != TY_BOOL) {
                if (lhs == TY_STR) {
                    /* str equality: byte-compare semantics, defined */
                    e->ty = TY_BOOL;
                    return TY_BOOL;
                }
                diag(e->span, E0202, SEV_ERROR, "can only compare numeric or bool values",
                     "composite equality arrives in P1.1");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            if (is_cmp && !ty_is_num(lhs)) {
                diag(e->span, E0202, SEV_ERROR, "'< > <= >=' need numeric operands",
                     "no cross-type comparisons exist in Tripon");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            e->ty = TY_BOOL;
            return TY_BOOL;
        }

        if (is_arith) {
            if (!ty_is_num(lhs) || !ty_is_num(rhs)) {
                char msg[260];
                snprintf(msg, sizeof msg, "%s and %s do not share an operator; no implicit conversion exists",
                         ty_name(lhs), ty_name(rhs));
                diag(e->span, E0202, SEV_ERROR, msg,
                     "types must match exactly; no implicit conversion exists; use cast<T>(x)");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            if (lhs != rhs) {
                char msg[260];
                snprintf(msg, sizeof msg, "%s and %s do not share an operator; no implicit conversion exists",
                         ty_name(lhs), ty_name(rhs));
                diag(e->span, E0202, SEV_ERROR, msg,
                     "types must match exactly; no implicit conversion exists; use cast<T>(x)");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            if (e->op == TK_PERCENT && !ty_is_int(lhs)) {
                diag(e->span, E0202, SEV_ERROR, "'%' needs integer operands",
                     "float remainder is not a thing here; use fmod via extern if needed");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            if (e->op == TK_POW && !ty_is_float(lhs)) {
                diag(e->span, E0202, SEV_ERROR, "'**' needs float operands in v0",
                     "integer pow lands with the stdlib math module");
                e->ty = TY_UNSET;
                return TY_UNSET;
            }
            e->ty = lhs;
            return lhs;
        }
        e->ty = TY_UNSET;
        return TY_UNSET;
    }
    }
    e->ty = TY_UNSET;
    return TY_UNSET;
}

/* ---------------- statements ---------------- */

static void tc_stmt(Ctx *c, Stmt *s);

static void tc_block(Ctx *c, Stmt **body, int nbody) {
    Scope keep;
    memset(&keep, 0, sizeof keep);
    keep.parent = c->scopes;
    c->scopes = (Scope *)arena_zalloc(c->a, sizeof(Scope));
    c->scopes->parent = keep.parent;
    for (int i = 0; i < nbody; i++) tc_stmt(c, body[i]);
    c->scopes = c->scopes->parent ? c->scopes->parent : NULL;
    c->scopes = keep.parent;
}

static void tc_stmt(Ctx *c, Stmt *s) {
    switch (s->kind) {
    case ST_LET: {
        Ty declared = s->ty;
        if (declared == TY_UNSET) {
            if (s->init) {
                declared = tc_expr(c, s->init);
            } else {
                diag(s->span, E0101, SEV_ERROR, "binding with no type and no initializer cannot be inferred",
                     "write 'let x: T;' for zero-init or 'let x = expr;'");
                declared = TY_UNSET;
            }
        } else if (s->init) {
            Ty it = tc_expr(c, s->init);
            if (it != declared) {
                char msg[240];
                snprintf(msg, sizeof msg, "initializer is %s but binding is %s; no implicit conversion exists",
                         it == TY_UNSET ? "unknown" : ty_name(it), ty_name(declared));
                diag(s->init->span, E0202, SEV_ERROR, msg,
                     "types must match exactly; no implicit conversion exists; use cast<T>(x)");
            }
        } else {
            /* typed, no init: zero-init is the defined default */
        }
        if (declared == TY_VOID) {
            diag(s->span, E0202, SEV_ERROR, "cannot bind a value of type ()",
                 "() is a unit type, not data");
            declared = TY_UNSET;
        }
        /* write back so codegen emits the inferred type */
        s->ty = declared;
        scope_insert(c, s->name, declared, s->is_const, s->span);
        break;
    }
    case ST_ASSIGN: {
        Ty tt = tc_expr(c, s->target);
        Ty vt = tc_expr(c, s->value);
        if (tt == TY_UNSET || vt == TY_UNSET) break;
        if (!s->target->lvalue) {
            diag(s->target->span, E0202, SEV_ERROR, "assignment target must be a binding",
                 "assign to a let/const-declared binding");
        }
        if (s->target->is_const_ref) {
            char msg[220];
            snprintf(msg, sizeof msg, "%s is const and cannot be reassigned", s->target->sval);
            diag(s->span, E0203, SEV_ERROR, msg, "declared const but reassigned; switch to let");
            break;
        }
        bool is_op = s->aop != TK_ASSIGN;
        if (tt != vt || (is_op && !ty_is_num(tt))) {
            char msg[260];
            snprintf(msg, sizeof msg, "cannot %s %s to %s; no implicit conversion exists",
                     is_op ? "apply op and assign" : "assign",
                     ty_name(vt), ty_name(tt));
            diag(s->value->span, E0202, SEV_ERROR, msg,
                 "types must match exactly; no implicit conversion exists; use cast<T>(x)");
        }
        break;
    }
    case ST_RETURN: {
        Ty fnret = c->cur_fn ? c->cur_fn->ret : TY_UNSET;
        Ty t = s->value ? tc_expr(c, s->value) : TY_VOID;
        if (s->value == NULL && fnret != TY_VOID && fnret != TY_UNSET) {
            diag(s->span, E0202, SEV_ERROR, "return needs a value; the fn returns one",
                 "bare return belongs in () fns");
            break;
        }
        if (s->value != NULL && fnret == TY_VOID) {
            diag(s->span, E0202, SEV_ERROR, "fn returns () but return carries a value",
                 "drop the value or change the fn return type");
            break;
        }
        if (s->value != NULL && t != fnret) {
            char msg[240];
            snprintf(msg, sizeof msg, "return is %s but fn returns %s; no implicit conversion exists",
                     t == TY_UNSET ? "unknown" : ty_name(t), ty_name(fnret));
            diag(s->value->span, E0202, SEV_ERROR, msg,
                 "types must match exactly; no implicit conversion exists; use cast<T>(x)");
        }
        break;
    }
    case ST_IF:
    case ST_WHILE: {
        Ty t = tc_expr(c, s->cond);
        if (t != TY_BOOL) {
            diag(s->cond->span, E0202, SEV_ERROR, "condition must be bool",
                 "no truthiness exists in Tripon; compare explicitly (x == 0)");
        }
        tc_block(c, s->body, s->nbody);
        if (s->els) {
            if (s->els->kind == ST_IF) tc_stmt(c, s->els);
            else tc_block(c, s->els->body, s->els->nbody);
        }
        break;
    }
    case ST_EXPR: {
        Ty t = tc_expr(c, s->value);
        (void)t;
        break;
    }
    case ST_BLOCK: {
        tc_block(c, s->body, s->nbody);
        break;
    }
    default: break;
    }
}

void typecheck_file(Arena *a, Decl **decls, int ndecls) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.a = a;

    prepass_fns(&c, decls, ndecls);

    bool have_main = false;
    for (int i = 0; i < c.nfuncs; i++)
        if (strcmp(c.funcs[i]->name, "main") == 0 && !c.funcs[i]->is_extern) {
            have_main = true;
            FnDecl *m = c.funcs[i];
            if (m->nparams != 0) {
                diag(m->span, E0501, SEV_ERROR, "fn main() takes no parameters",
                     "args access arrives with the runtime module");
            }
            if (m->ret != TY_I32 && m->ret != TY_VOID) {
                diag(m->span, E0501, SEV_ERROR, "fn main() must return i32 or ()",
                     "i32 becomes the process exit code");
            }
            c.cur_fn = m;
            c.scopes = NULL;
            tc_block(&c, m->body, m->nbody);
            c.cur_fn = NULL;
        }
    if (!have_main) {
        Span sp = {0};
        sp.line = 1; sp.col = 1; sp.len = 1;
        diag(sp, E0501, SEV_ERROR, "no fn main() found in this file",
             "define fn main() -> i32 or mark an explicit entry target");
    }

    /* typecheck the rest of the non-main functions too */
    for (int i = 0; i < c.nfuncs; i++) {
        if (strcmp(c.funcs[i]->name, "main") == 0 || c.funcs[i]->is_extern) continue;
        FnDecl *fn = c.funcs[i];
        c.cur_fn = fn;
        c.scopes = NULL;
        c.scopes = arena_zalloc(c.a, sizeof(Scope));
        for (int k = 0; k < fn->nparams; k++) {
            scope_insert(&c, fn->params[k].name, fn->params[k].ty, false, fn->params[k].span);
        }
        for (int k = 0; k < fn->nbody; k++) tc_stmt(&c, fn->body[k]);
        c.cur_fn = NULL;
    }
}

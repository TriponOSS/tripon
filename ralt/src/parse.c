/* parse.c - recursive descent per spec/grammar.ebnf (P1.0 subset).
 * Structs/enums/match parse now, error with "lands in P1.1" diagnostics. */
#include "common.h"

typedef struct {
    Token *toks;
    int ntoks, pos;
    Arena *a;
    const char *file;
} Parser;

static Token *peek(Parser *p) { return &p->toks[p->pos]; }
static Token *peekn(Parser *p, int n) { int i = p->pos + n; if (i >= p->ntoks) i = p->ntoks - 1; return &p->toks[i]; }

static Token *advance(Parser *p) {
    Token *t = &p->toks[p->pos];
    if (t->kind != TK_EOF) p->pos++;
    return t;
}

static bool at(Parser *p, TokKind k) { return peek(p)->kind == k; }

static bool eat(Parser *p, TokKind k) {
    if (at(p, k)) { advance(p); return true; }
    return false;
}

static Token *expect_token(Parser *p, TokKind k, const char *what) {
    if (at(p, k)) return advance(p);
    Span sp = peek(p)->span;
    char msg[220];
    snprintf(msg, sizeof msg, "expected %s, got %s", what, tok_kind_word(peek(p)->kind));
    diag(sp, E0101, SEV_ERROR, msg, "see spec/grammar.ebnf for the legal shapes here");
    return &p->toks[p->pos];
}

/* ---------------- types ---------------- */

static Ty parse_type(Parser *p) {
    if (eat(p, TK_LP) && eat(p, TK_RP)) {
        return TY_VOID;     /* "()" = unit/void */
    }
    Token *t = expect_token(p, TK_IDENT, "a type name");
    static const struct { const char *n; Ty ty; } TYPES[] = {
        {"i8", TY_I8}, {"i16", TY_I16}, {"i32", TY_I32}, {"i64", TY_I64},
        {"u8", TY_U8}, {"u16", TY_U16}, {"u32", TY_U32}, {"u64", TY_U64},
        {"isize", TY_ISIZE}, {"usize", TY_USIZE},
        {"f32", TY_F32}, {"f64", TY_F64}, {"bool", TY_BOOL}, {"str", TY_STR},
        {"()", TY_VOID},
    };
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++)
        if (strcmp(TYPES[i].n, t->text) == 0) return TYPES[i].ty;
    diag(t->span, E0101, SEV_ERROR, "unknown type name",
         "builtins: i8 i16 i32 i64 u8 u16 u32 u64 isize usize f32 f64 bool str ()");
    return TY_UNSET;
}

/* ---------------- expressions ---------------- */

static Expr *parse_expr(Parser *p);

static Expr *new_expr(Parser *p, ExKind k, Span sp) {
    Expr *e = arena_zalloc(p->a, sizeof(Expr));
    e->kind = k;
    e->span = sp;
    e->ty = TY_UNSET;
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case TK_INT: {
        Expr *e = new_expr(p, EX_INT, t->span);
        advance(p);
        e->ival = t->ival;
        return e;
    }
    case TK_FLOAT: {
        Expr *e = new_expr(p, EX_FLOAT, t->span);
        advance(p);
        e->fval = t->fval;
        return e;
    }
    case TK_STR: {
        Expr *e = new_expr(p, EX_STR, t->span);
        advance(p);
        e->sval = t->text;
        e->slen = t->len;
        e->ty = TY_STR;
        return e;
    }
    case TK_TRUE:
    case TK_FALSE: {
        Expr *e = new_expr(p, EX_BOOL, t->span);
        advance(p);
        e->bval = t->kind == TK_TRUE;
        e->ty = TY_BOOL;
        return e;
    }
    case TK_IDENT: {
        Expr *e = new_expr(p, EX_IDENT, t->span);
        advance(p);
        e->sval = t->text;
        e->ty = TY_UNSET;
        /* call: ident ( args ) */
        if (at(p, TK_LP)) {
            advance(p);
            e->kind = EX_CALL;
            e->callee = e->sval;
            while (!at(p, TK_RP) && !at(p, TK_EOF)) {
                Expr **na = realloc(e->args == NULL ? malloc(0) : e->args,
                                    (size_t)(e->nargs + 1) * sizeof(Expr *));
                if (!na) exit(1);
                e->args = na;
                e->args[e->nargs++] = parse_expr(p);
                if (!eat(p, TK_COMMA)) break;
            }
            expect_token(p, TK_RP, "')' to close the call");
        }
        return e;
    }
    case TK_MATCH: {
        diag(t->span, E0101, SEV_ERROR, "match is parsed from P1.1",
             "spec/grammar.ebnf defines it; ralt compiles it in P1.1");
        advance(p);
        return new_expr(p, EX_INT, t->span);
    }
    case TK_LP: {
        advance(p);
        Expr *e = parse_expr(p);
        expect_token(p, TK_RP, "')' to close the group");
        return e;
    }
    default: {
        Span sp = t->span;
        char msg[120];
        snprintf(msg, sizeof msg, "expected an expression, got %s", tok_kind_word(t->kind));
        diag(sp, E0101, SEV_ERROR, msg, "primary forms: literal, ident, call, (expr)");
        advance(p);
        return new_expr(p, EX_INT, sp);
    }
    }
}

static Expr *parse_unary(Parser *p) {
    Token *t = peek(p);
    if (t->kind == TK_NOT || t->kind == TK_MINUS || t->kind == TK_AMP) {
        Expr *e = new_expr(p, EX_UN, t->span);
        advance(p);
        e->unop = t->kind == TK_NOT ? UN_NOT : t->kind == TK_MINUS ? UN_NEG : UN_ADDR;
        if (e->unop == UN_ADDR) {
            diag(t->span, E0101, SEV_ERROR, "'&' address-of arrives with borrows (P1.1+)",
                 "pointer-free core until borrow rules land");
        }
        e->operand = parse_unary(p);
        return e;
    }
    /* postfix: primary, then chains */
    Expr *e = parse_primary(p);
    for (;;) {
        if (at(p, TK_DOT)) {
            diag(peek(p)->span, E0101, SEV_ERROR, "member access lands with structs (P1.1)",
                 "no methods exist on builtins yet");
            advance(p);
            advance(p);
            continue;
        }
        if (at(p, TK_LB)) {
            diag(peek(p)->span, E0101, SEV_ERROR, "indexing lands with slices (P1.2)",
                 "fixed arrays arrive with structs in P1.1");
            advance(p);
            advance(p);
            continue;
        }
        break;
    }
    return e;
}

static Expr *parse_binary(Parser *p, int min_prec) {
    Expr *lhs = parse_unary(p);
    for (;;) {
        TokKind k = peek(p)->kind;
        int prec = -1;
        bool right_assoc = false;
        switch (k) {
        case TK_OR: prec = 1; break;
        case TK_AND: prec = 2; break;
        case TK_EQ: case TK_NE: prec = 3; break;
        case TK_LT: case TK_GT: case TK_LE: case TK_GE: prec = 4; break;
        case TK_PLUS: case TK_MINUS: prec = 5; break;
        case TK_STAR: case TK_SLASH: case TK_PERCENT: prec = 6; break;
        case TK_POW: prec = 7; right_assoc = true; break;
        default: break;
        }
        if (prec < min_prec || prec < 0) break;
        Token *op = advance(p);
        (void)op;
        Expr *rhs = parse_binary(p, right_assoc ? prec : prec + 1);
        Expr *e = new_expr(p, EX_BIN, op->span);
        e->op = k;
        e->lhs = lhs;
        e->rhs = rhs;
        /* diag span covers the whole operator expression (same line) */
        e->span = lhs->span;
        if (rhs->span.line == lhs->span.line)
            e->span.len = rhs->span.col + rhs->span.len - lhs->span.col;
        lhs = e;
    }
    return lhs;
}

static Expr *parse_expr(Parser *p) { return parse_binary(p, 1); }

/* ---------------- statements ---------------- */

static Stmt *parse_stmt(Parser *p);

static void push_stmt(Stmt ***arr, int *n, Stmt *s, Arena *a) {
    Stmt **na = arena_alloc(a, sizeof(Stmt *) * (size_t)(*n + 1));
    if (*n) memcpy(na, *arr, sizeof(Stmt *) * (size_t)*n);
    na[*n] = s;
    *arr = na;
    (*n)++;
}

static Stmt *new_stmt(Parser *p, StKind k, Span sp) {
    Stmt *s = arena_zalloc(p->a, sizeof(Stmt));
    s->kind = k;
    s->span = sp;
    return s;
}

static Stmt *parse_block_stmt(Parser *p);  /* always returns a ST_BLOCK */

static Stmt *parse_if(Parser *p) {
    Token *ifkw = advance(p); /* if */
    Stmt *s = new_stmt(p, ST_IF, ifkw->span);
    s->cond = parse_expr(p);
    Stmt *blk = parse_block_stmt(p);
    s->body = blk->body;
    s->nbody = blk->nbody;
    if (eat(p, TK_ELSE)) {
        if (at(p, TK_IF)) s->els = parse_if(p);
        else s->els = parse_block_stmt(p);
    }
    return s;
}

static Stmt *parse_while(Parser *p) {
    Token *kw = advance(p);
    Stmt *s = new_stmt(p, ST_WHILE, kw->span);
    s->cond = parse_expr(p);
    Stmt *blk = parse_block_stmt(p);
    s->body = blk->body;
    s->nbody = blk->nbody;
    return s;
}

static Stmt *parse_var_decl(Parser *p) {
    Token *kw = advance(p); /* let | const */
    Stmt *s = new_stmt(p, ST_LET, kw->span);
    s->is_const = kw->kind == TK_CONST;
    s->ty = TY_UNSET;          /* zalloc is 0; TY_I8 is 0 - be explicit */
    Token *name = expect_token(p, TK_IDENT, "a binding name after let/const");
    s->name = name->text;
    s->span = kw->span;
    if (eat(p, TK_COLON)) s->ty = parse_type(p);
    if (eat(p, TK_ASSIGN)) s->init = parse_expr(p);
    expect_token(p, TK_SEMI, "';' to end the statement");
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case TK_LET:
    case TK_CONST:
        return parse_var_decl(p);
    case TK_RETURN: {
        advance(p);
        Stmt *s = new_stmt(p, ST_RETURN, t->span);
        if (!at(p, TK_SEMI)) s->value = parse_expr(p);
        expect_token(p, TK_SEMI, "';' after return");
        return s;
    }
    case TK_IF:
        return parse_if(p);
    case TK_WHILE:
        return parse_while(p);
    case TK_BRACE_L:
        return parse_block_stmt(p);
    case TK_MATCH: {
        diag(t->span, E0101, SEV_ERROR, "match lands in P1.1",
             "spec/grammar.ebnf has the final shape; compiler support next slice");
        advance(p);
        return new_stmt(p, ST_BLOCK, t->span);
    }
    case TK_SEMI:  /* empty statement: allowed, no-op */
        advance(p);
        return new_stmt(p, ST_BLOCK, t->span);
    default: {
        Expr *e = parse_expr(p);
        if (peek(p)->kind == TK_ASSIGN || peek(p)->kind == TK_ADDEQ ||
            peek(p)->kind == TK_SUBEQ || peek(p)->kind == TK_MULEQ ||
            peek(p)->kind == TK_DIVEQ || peek(p)->kind == TK_MODEQ) {
            Token *op = advance(p);
            Stmt *s = new_stmt(p, ST_ASSIGN, e->span);
            s->target = e;
            s->aop = op->kind;
            s->value = parse_expr(p);
            expect_token(p, TK_SEMI, "';' to end the statement");
            return s;
        }
        expect_token(p, TK_SEMI, "';' to end the statement");
        Stmt *s = new_stmt(p, ST_EXPR, e->span);
        s->value = e;
        return s;
    }
    }
}

static Stmt *parse_block_stmt(Parser *p) {
    expect_token(p, TK_BRACE_L, "'{'");
    Stmt *s = new_stmt(p, ST_BLOCK, peekn(p, -1)->span);
    while (!at(p, TK_BRACE_R) && !at(p, TK_EOF)) {
        push_stmt(&s->body, &s->nbody, parse_stmt(p), p->a);
    }
    expect_token(p, TK_BRACE_R, "'}' or statement");
    return s;
}

/* ---------------- top level ---------------- */

static FnDecl *parse_fn_proto(Parser *p, bool extern_mode) {
    Token *kw = expect_token(p, TK_FN, "'fn'");
    Token *name = expect_token(p, TK_IDENT, "a function name");
    FnDecl *fn = arena_zalloc(p->a, sizeof(FnDecl));
    fn->name = name->text;
    fn->span = kw->span;
    expect_token(p, TK_LP, "'(' after fn name");
    while (!at(p, TK_RP) && !at(p, TK_EOF)) {
        Token *pn = expect_token(p, TK_IDENT, "a parameter name");
        expect_token(p, TK_COLON, "':' before the parameter type");
        Param *np = arena_alloc(p->a, sizeof(Param));
        np->name = pn->text;
        np->span = pn->span;
        np->ty = parse_type(p);
        Param *na = realloc(fn->params == NULL ? malloc(0) : fn->params,
                            sizeof(Param) * (size_t)(fn->nparams + 1));
        if (!na) exit(1);
        fn->params = na;
        fn->params[fn->nparams++] = *np;
        if (!eat(p, TK_COMMA)) break;
    }
    expect_token(p, TK_RP, "')' to close parameters");
    if (eat(p, TK_ARROW)) fn->ret = parse_type(p);
    else fn->ret = TY_VOID;
    fn->is_extern = extern_mode;
    return fn;
}

Decl **parse_file(Arena *a, Token *toks, int ntoks, const char *file) {
    Parser p;
    memset(&p, 0, sizeof p);
    p.toks = toks;
    p.ntoks = ntoks;
    p.a = a;
    p.file = file;

    VecDecl out;
    memset(&out, 0, sizeof out);

    /* shebang line: ignore a #! prefix (lexer skips to first real token) */

    while (!at(&p, TK_EOF)) {
        if (at(&p, TK_USE)) {
            Token *usekw = advance(&p);
            expect_token(&p, TK_TIER, "'tier' after use");
            Token *tname = expect_token(&p, TK_IDENT, "a tier name (kernel|core|std|full)");
            /* tier check: parsed but honored in P1.1 module model; record now */
            (void)usekw;
            Decl *d = arena_zalloc(a, sizeof(Decl));
            d->kind = D_TIER;
            d->name = tname->text;
            d->span = usekw->span;
            vec_push_ptr((VecPtr *)&out, d);
            expect_token(&p, TK_SEMI, "';' after tier name");
            continue;
        }
        if (at(&p, TK_EXTERN)) {
            Token *ekw = advance(&p);
            Token *lit = expect_token(&p, TK_STR, "an ABI string like \"C\" after extern");
            (void)ekw;
            if (lit->len != 1 || lit->text[0] != 'C') {
                diag(lit->span, E0101, SEV_ERROR, "only extern \"C\" is supported in P1",
                     "other ABIs arrive with more backends");
            }
            expect_token(&p, TK_BRACE_L, "'{' to open the extern block");
            while (!at(&p, TK_BRACE_R) && !at(&p, TK_EOF)) {
                FnDecl *fn = parse_fn_proto(&p, true);
                expect_token(&p, TK_SEMI, "';' after extern fn signature");
                Decl *d = arena_zalloc(a, sizeof(Decl));
                d->kind = D_FN;
                d->fn = fn;
                vec_push_ptr((VecPtr *)&out, d);
            }
            expect_token(&p, TK_BRACE_R, "'}' to close the extern block");
            continue;
        }
        if (at(&p, TK_FN)) {
            FnDecl *fn = parse_fn_proto(&p, false);
            fn->body = NULL;
            fn->nbody = 0;
            Stmt *blk = parse_block_stmt(&p);
            fn->body = blk->body;
            fn->nbody = blk->nbody;
            Decl *d = arena_zalloc(a, sizeof(Decl));
            d->kind = D_FN;
            d->fn = fn;
            vec_push_ptr((VecPtr *)&out, d);
            continue;
        }
        if (at(&p, TK_STRUCT) || at(&p, TK_ENUM)) {
            diag(peek(&p)->span, E0101, SEV_ERROR, "struct/enum land in P1.1",
                 "they are in spec/grammar.ebnf; compiler support is the next slice");
            break;
        }
        Span sp = peek(&p)->span;
        char msg[160];
        snprintf(msg, sizeof msg, "expected a top-level declaration, got %s", tok_kind_word(peek(&p)->kind));
        diag(sp, E0101, SEV_ERROR, msg, "top level: use tier, extern blocks, fn");
        break;
    }

    vec_push_ptr((VecPtr *)&out, NULL);   /* NULL-terminate for the driver */
    return (Decl **)out.data;
}

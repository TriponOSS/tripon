/* fmt.c - `ralt fmt`: parse, re-print canonical layout.
 * Agent rule: fmt output is stable and idempotent. */
#include "common.h"

typedef struct {
    FILE *out;
    int indent;
} Fm;

static int prec_of(TokKind k) {
    switch (k) {
    case TK_OR: return 1;
    case TK_AND: return 2;
    case TK_EQ: case TK_NE: return 3;
    case TK_LT: case TK_GT: case TK_LE: case TK_GE: return 4;
    case TK_PLUS: case TK_MINUS: return 5;
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 6;
    case TK_POW: return 7;
    default: return 0;
    }
}

static const char *op_word(TokKind k) {
    switch (k) {
    case TK_OR: return "||";
    case TK_AND: return "&&";
    case TK_EQ: return "==";
    case TK_NE: return "!=";
    case TK_LT: return "<";
    case TK_GT: return ">";
    case TK_LE: return "<=";
    case TK_GE: return ">=";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_PERCENT: return "%";
    case TK_POW: return "**";
    default: return "?";
    }
}

static void fm_indent(Fm *f) {
    for (int i = 0; i < f->indent * 2; i++) fputc(' ', f->out);
}

static void fm_type(Fm *f, Ty t) { fputs(ty_name(t), f->out); }

static void fm_expr(Fm *f, Expr *e, int parent_prec) {
    switch (e->kind) {
    case EX_INT:
        fprintf(f->out, "%lld%s", (long long)e->ival, e->ty == TY_I64 ? "" : "");
        break;
    case EX_FLOAT:
        fprintf(f->out, "%.17g", e->fval);
        break;
    case EX_STR: {
        fputc('"', f->out);
        for (size_t i = 0; i < e->slen; i++) {
            unsigned char c = (unsigned char)e->sval[i];
            switch (c) {
            case '\n': fputs("\\n", f->out); break;
            case '\t': fputs("\\t", f->out); break;
            case '\r': fputs("\\r", f->out); break;
            case '\0': fputs("\\0", f->out); break;
            case '"':  fputs("\\\"", f->out); break;
            case '\\': fputs("\\\\", f->out); break;
            default: fputc(c, f->out);
            }
        }
        fputc('"', f->out);
        break;
    }
    case EX_BOOL:
        fputs(e->bval ? "true" : "false", f->out);
        break;
    case EX_IDENT:
        fputs(e->sval, f->out);
        break;
    case EX_CALL:
        fputs(e->callee, f->out);
        fputc('(', f->out);
        for (int i = 0; i < e->nargs; i++) {
            if (i) fputs(", ", f->out);
            fm_expr(f, e->args[i], 0);
        }
        fputc(')', f->out);
        break;
    case EX_UN:
        if (e->unop == UN_NOT) fputc('!', f->out);
        else fputc('-', f->out);
        fm_expr(f, e->operand, 100);
        break;
    case EX_BIN: {
        int my = prec_of(e->op);
        bool wrap = my < parent_prec;
        if (wrap) fputc('(', f->out);
        fm_expr(f, e->lhs, my);
        fprintf(f->out, " %s ", op_word(e->op));
        fm_expr(f, e->rhs, my + 1);
        if (wrap) fputc(')', f->out);
        break;
    }
    default: break;
    }
}

static void fm_stmts(Fm *f, Stmt **body, int nbody);

static void fm_if(Fm *f, Stmt *s) {   /* prints the whole if/else-if/else chain inline */
    fputs("if ", f->out);
    fm_expr(f, s->cond, 0);
    fputs(" {\n", f->out);
    f->indent++;
    fm_stmts(f, s->body, s->nbody);
    f->indent--;
    fm_indent(f);
    fputs("}", f->out);
    if (s->els) {
        if (s->els->kind == ST_IF) {
            fputs(" else ", f->out);
            fm_if(f, s->els);
        } else {
            fputs(" else {\n", f->out);
            f->indent++;
            fm_stmts(f, s->els->body, s->els->nbody);
            f->indent--;
            fm_indent(f);
            fputs("}", f->out);
        }
    }
}

static void fm_stmt(Fm *f, Stmt *s) {
    switch (s->kind) {
    case ST_LET:
        fm_indent(f);
        fputs(s->is_const ? "const " : "let ", f->out);
        fputs(s->name, f->out);
        if (s->ty != TY_UNSET) {
            fputs(": ", f->out);
            fm_type(f, s->ty);
        }
        if (s->init) {
            fputs(" = ", f->out);
            fm_expr(f, s->init, 0);
        }
        fputs(";\n", f->out);
        break;
    case ST_ASSIGN:
        fm_indent(f);
        fm_expr(f, s->target, 0);
        fprintf(f->out, " %s ", op_word(s->aop) ? (s->aop == TK_ASSIGN ? "=" :
                s->aop == TK_ADDEQ ? "+=" : s->aop == TK_SUBEQ ? "-=" :
                s->aop == TK_MULEQ ? "*=" : s->aop == TK_DIVEQ ? "/=" : "%=") : "=");
        fm_expr(f, s->value, 0);
        fputs(";\n", f->out);
        break;
    case ST_RETURN:
        fm_indent(f);
        fputs("return", f->out);
        if (s->value) {
            fputc(' ', f->out);
            fm_expr(f, s->value, 0);
        }
        fputs(";\n", f->out);
        break;
    case ST_IF:
        fm_indent(f);
        fm_if(f, s);
        fputs("\n", f->out);
        break;
    case ST_WHILE:
        fm_indent(f);
        fputs("while ", f->out);
        fm_expr(f, s->cond, 0);
        fputs(" {\n", f->out);
        f->indent++;
        fm_stmts(f, s->body, s->nbody);
        f->indent--;
        fm_indent(f);
        fputs("}\n", f->out);
        break;
    case ST_EXPR:
        fm_indent(f);
        fm_expr(f, s->value, 0);
        fputs(";\n", f->out);
        break;
    case ST_BLOCK:
        fm_indent(f);
        fputs("{\n", f->out);
        f->indent++;
        fm_stmts(f, s->body, s->nbody);
        f->indent--;
        fm_indent(f);
        fputs("}\n", f->out);
        break;
    default: break;
    }
}

static void fm_stmts(Fm *f, Stmt **body, int nbody) {
    for (int i = 0; i < nbody; i++) fm_stmt(f, body[i]);
}

void fmt_file(Arena *a, Decl **decls, int ndecls, FILE *out) {
    Fm f;
    f.out = out;
    f.indent = 0;
    for (int i = 0; i < ndecls; i++) {
        Decl *d = decls[i];
        if (d->kind == D_TIER) {
            fprintf(out, "use tier %s;\n", d->name);
        } else if (d->kind == D_FN) {
            FnDecl *fn = d->fn;
            if (fn->is_extern) {
                fprintf(out, "extern \"C\" {\n");
                fprintf(out, "    fn %s(", fn->name);
                for (int k = 0; k < fn->nparams; k++) {
                    if (k) fputs(", ", out);
                    fprintf(out, "%s: %s", fn->params[k].name, ty_name(fn->params[k].ty));
                }
                fprintf(out, ")");
                if (fn->ret != TY_VOID) {
                    fprintf(out, " -> %s", ty_name(fn->ret));
                }
                fprintf(out, ";\n}\n");
            } else {
                fprintf(out, "fn %s(", fn->name);
                for (int k = 0; k < fn->nparams; k++) {
                    if (k) fputs(", ", out);
                    fprintf(out, "%s: %s", fn->params[k].name, ty_name(fn->params[k].ty));
                }
                fprintf(out, ")");
                if (fn->ret != TY_VOID) {
                    fprintf(out, " -> %s", ty_name(fn->ret));
                }
                fputs(" {\n", out);
                f.indent = 1;
                fm_stmts(&f, fn->body, fn->nbody);
                fputs("}\n", out);
            }
        }
        (void)a;
        fputs("\n", out);
    }
}

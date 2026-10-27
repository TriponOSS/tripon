/* main.c - ralt driver. build | run | check | emit | explain. */
#include "common.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(void) {
    fputs(
        "ralt - the Tripon toolchain (.tp files)\n"
        "\n"
        "  ralt build <file.tp> [-o out]  [-O fast] [-fast]  build an executable\n"
        "  ralt run   <file.tp> [args...]                  build + execute\n"
        "  ralt check <file.tp> [--json]                   typecheck with diagnostics\n"
        "  ralt emit  --c file.tp -o out.c                 stop after C codegen\n"
        "  ralt explain <code>                             e.g. E0202\n"
        "\n"
        "  -O fast   enable opt-level code paths in the C pipeline\n"
        "  -fast     integer overflow wraps instead of trapping (no-UB stays)\n",
        stderr);
}

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); exit(1); }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void mkdir_p(const char *dir) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* pipeline shared by build/check: parse + typecheck */
static Decl **analyze(Arena *a, const char *path, int *ndecls) {
    size_t len;
    char *src = read_all(path, &len);
    if (!src) {
        fprintf(stderr, "ralt: cannot read %s\n", path);
        exit(2);
    }
    int ntoks;
    Token *toks = lex_file(a, src, len, path, &ntoks);
    Decl **decls = parse_file(a, toks, ntoks, path);
    int n = 0;
    while (decls && decls[n]) n++;
    *ndecls = n;
    if (g_nerrors == 0) typecheck_file(a, decls, n);
    return decls;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];

    if (strcmp(cmd, "explain") == 0) {
        if (argc < 3) { usage(); return 2; }
        const char *arg = argv[2];
        if (arg[0] == 'E' || arg[0] == 'e') arg++;
        int code = atoi(arg);
        const ErrEntry *e = err_lookup(code);
        printf("E%04d\nseverity: %s\nname: %s\nhint: %s\n", code,
               e->sev == SEV_ERROR ? "error" : e->sev == SEV_WARN ? "warn" : "note",
               e->name, e->hint);
        return strcmp(e->name, "internal") == 0 && code != 0 ? 1 : 0;
    }

    const char *file = NULL;
    const char *outname = NULL;
    bool json = false;
    bool fast = false;
    bool ofast = false;
    bool want_emit = false;

    for (int i = 2; i < argc; i++) {
        const char *s = argv[i];
        if (s[0] != '-') { file = s; continue; }
        if (strcmp(s, "--json") == 0) json = true;
        else if (strcmp(s, "-fast") == 0) fast = true;
        else if (strcmp(s, "-O") == 0 && i + 1 < argc) { i++; ofast = strcmp(argv[i], "fast") == 0; }
        else if (strcmp(s, "-o") == 0 && i + 1 < argc) outname = argv[++i];
        else if (strcmp(s, "--c") == 0) want_emit = true;
        else { fprintf(stderr, "ralt: unknown option %s\n", s); return 2; }
    }
    (void)want_emit;
    if (!file) { usage(); return 2; }

    Arena *a = arena_new();
    if (!a) { fputs("ralt: arena init failed\n", stderr); return 2; }

    if (strcmp(cmd, "emit") == 0) {
        int ndecls;
        Decl **decls = analyze(a, file, &ndecls);
        const char *outpath = outname ? outname : "build/out.c";
        char dirbuf[1024];
        snprintf(dirbuf, sizeof dirbuf, "%s", outpath);
        char *slash = strrchr(dirbuf, '/');
        if (slash) { *slash = 0; mkdir_p(dirbuf); }
        codegen_file(a, decls, ndecls, outpath, fast);
        return 0;
    }

    if (strcmp(cmd, "check") == 0) {
        int ndecls;
        Decl **decls = analyze(a, file, &ndecls);
        (void)decls; (void)ndecls;
        print_diags(json);
        return g_nerrors > 0 ? 1 : 0;
    }

    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "run") == 0) {
        int ndecls;
        Decl **decls = analyze(a, file, &ndecls);
        if (g_nerrors > 0) {
            print_diags(false);
            fprintf(stderr, "ralt: %d error(s), build failed\n", g_nerrors);
            return 1;
        }
        mkdir_p("build");
        const char *slash = strrchr(file, '/');
        const char *stem = slash ? slash + 1 : file;
        char cpath[1024], binpath[1024];
        {
            const char *dot = strrchr(stem, '.');
            int stemlen = dot ? (int)(dot - stem) : (int)strlen(stem);
            snprintf(cpath, sizeof cpath, "build/%.*s.c", stemlen, stem);
        }
        snprintf(binpath, sizeof binpath, "%s", outname ? outname : "build/a.out");
        codegen_file(a, decls, ndecls, cpath, fast);

        /* cc stage: -O0 default (builder speed), -O fast -> -O2; -lm for pow */
        pid_t pid = fork();
        if (pid == 0) {
            const char *cc = getenv("CC");
            if (!cc || !*cc) cc = "cc";
            execlp(cc, cc, "-std=c99", ofast ? "-O2" : "-O0",
                   "-o", binpath, cpath, "-lm", (char *)NULL);
            fprintf(stderr, "ralt: failed to exec %s (is a C toolchain installed?)\n", cc);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        int st = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        if (st != 0) {
            fprintf(stderr, "ralt: cc pipeline exited %d\n", st);
            return 1;
        }
        if (cmd[0] == 'r') {
            char **child = malloc(sizeof(char *) * (size_t)argc);
            int n = 0;
            child[n++] = (char *)binpath;
            for (int i = 3; i < argc; i++) child[n++] = argv[i];
            child[n] = NULL;
            execv(binpath, child);
            fprintf(stderr, "ralt: failed to exec %s\n", binpath);
            return 127;
        }
        printf("%s\n", binpath);
        return 0;
    }

    usage();
    return 2;
}

// Manual duplex for the HP P1005 (no duplexer). Usage: duplex-print file.pdf [--dry-run] | --selftest
// Pass 1: odd pages (printer outputs face-down). User puts the stack back as-is.
// Pass 2: even pages in REVERSE (top sheet = last sheet), rotated 180 (P1005 backs came out upside down).
// Odd page count: a blank page goes first so the backs line up with the sheets.
// Env: DUPLEX_PRINTER (default HP_LaserJet_P1005), DUPLEX_ROTATE_BACKS (default 180, 0 disables).
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run(char *const av[], char *out, size_t n) { // exec without a shell (file names are untrusted)
    int p[2]; pipe(p);
    pid_t c = fork();
    if (!c) { dup2(p[1], 1); close(p[0]); close(p[1]); execvp(av[0], av); _exit(127); }
    close(p[1]);
    size_t len = 0; ssize_t r;
    while (out && len < n - 1 && (r = read(p[0], out + len, n - 1 - len)) > 0) len += r;
    if (out) out[len] = 0;
    close(p[0]);
    int st; waitpid(c, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static void die(const char *m) {
    fprintf(stderr, "duplex-print: %s\n", m);
    char *z[] = {"zenity", "--error", "--text", (char *)m, 0};
    run(z, 0, 0);
    exit(1);
}

// front: "1,3,5"; back: even pages descending; *blank=1 when the last sheet has no back
static void plan(int n, char *front, char *back, int *blank) {
    front[0] = back[0] = 0;
    for (int i = 1; i <= n; i += 2) sprintf(front + strlen(front), "%s%d", i > 1 ? "," : "", i);
    for (int i = n / 2 * 2; i >= 2; i -= 2) sprintf(back + strlen(back), "%s%d", i < n / 2 * 2 ? "," : "", i);
    *blank = n % 2 && n > 1;
}

static void selftest(void) {
    char f[256], b[256]; int bl;
    plan(5, f, b, &bl); assert(!strcmp(f, "1,3,5") && !strcmp(b, "4,2") && bl);
    plan(6, f, b, &bl); assert(!strcmp(f, "1,3,5") && !strcmp(b, "6,4,2") && !bl);
    plan(4, f, b, &bl); assert(!strcmp(f, "1,3") && !strcmp(b, "4,2") && !bl);
    plan(1, f, b, &bl); assert(!strcmp(f, "1") && !b[0] && !bl);
    puts("selftest ok");
}

static void print_wait(const char *printer, const char *pdf) {
    char out[512], id[128] = "", q[256];
    char *lp[] = {"lp", "-d", (char *)printer, "-o", "media=A4", (char *)pdf, 0};
    if (run(lp, out, sizeof out)) die("lp failed");
    char *s = strstr(out, "request id is ");
    if (!s || sscanf(s + 14, "%127s", id) != 1) die("lp gave no job id");
    snprintf(q, sizeof q, "%s ", id);
    for (int i = 0; i < 600; i++) { // 20 min
        char *ls[] = {"lpstat", "-W", "not-completed", "-o", (char *)printer, 0};
        char l[4096]; run(ls, l, sizeof l);
        if (!strstr(l, q)) return;
        sleep(2);
    }
    die("job did not finish in 20 min");
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--selftest")) { selftest(); return 0; }
    if (argc < 2) die("usage: duplex-print file.pdf [--dry-run]");
    const char *f = argv[1], *printer = getenv("DUPLEX_PRINTER") ? getenv("DUPLEX_PRINTER") : "HP_LaserJet_P1005";
    const char *rot = getenv("DUPLEX_ROTATE_BACKS") ? getenv("DUPLEX_ROTATE_BACKS") : "180";
    int dry = argc > 2 && !strcmp(argv[2], "--dry-run");
    size_t L = strlen(f);
    if (L < 4 || strcasecmp(f + L - 4, ".pdf") || access(f, R_OK)) die("need a readable .pdf file");

    char info[4096], *s;
    char *pi[] = {"pdfinfo", (char *)f, 0};
    if (run(pi, info, sizeof info) || !(s = strstr(info, "Pages:"))) die("cannot read page count");
    int n = atoi(s + 6); double w = 595.276, h = 841.89;
    if (n < 1) die("cannot read page count");
    if ((s = strstr(info, "Page size:"))) sscanf(s + 10, "%lf x %lf", &w, &h);

    char dir[] = "/tmp/duplexXXXXXX", front[4096], back[4096], pf[64], pb[64], pbl[64], pt[64];
    int blank;
    if (!mkdtemp(dir)) die("mkdtemp failed");
    plan(n, front, back, &blank);
    snprintf(pf, sizeof pf, "%s/front.pdf", dir); snprintf(pb, sizeof pb, "%s/back.pdf", dir);
    snprintf(pbl, sizeof pbl, "%s/blank.pdf", dir); snprintf(pt, sizeof pt, "%s/rot.pdf", dir);
    char *qf[] = {"qpdf", "--empty", "--pages", (char *)f, front, "--", pf, 0};
    if (run(qf, 0, 0)) die("qpdf failed (front)");
    if (n > 1) {
        if (blank) {
            char W[64], H[64]; snprintf(W, sizeof W, "-dDEVICEWIDTHPOINTS=%g", w); snprintf(H, sizeof H, "-dDEVICEHEIGHTPOINTS=%g", h);
            char *gs[] = {"gs", "-q", "-o", pbl, "-sDEVICE=pdfwrite", W, H, "-c", "showpage", 0};
            if (run(gs, 0, 0)) die("gs blank page failed");
            char *qb[] = {"qpdf", "--empty", "--pages", pbl, "1", (char *)f, back, "--", pb, 0};
            if (run(qb, 0, 0)) die("qpdf failed (back)");
        } else {
            char *qb[] = {"qpdf", "--empty", "--pages", (char *)f, back, "--", pb, 0};
            if (run(qb, 0, 0)) die("qpdf failed (back)");
        }
        if (strcmp(rot, "0")) {
            char r[32]; snprintf(r, sizeof r, "--rotate=+%s", rot);
            char *qr[] = {"qpdf", pb, r, "--", pt, 0};
            if (run(qr, 0, 0)) die("qpdf failed (rotate)");
            rename(pt, pb);
        }
    }
    if (dry) { printf("dry run: front=%s back=%s%s files in %s (nothing printed)\n", front, blank ? "blank," : "", back, dir); return 0; }
    print_wait(printer, pf);
    if (n > 1) {
        char *zq[] = {"zenity", "--question", "--title=Duplex step 2", "--ok-label=Print backs", "--text",
            "Take the printed stack from the output tray and put it into the input tray AS IS (don't reorder, don't flip): blank side up, same top edge direction.\n\nThen press Print backs.", 0};
        if (run(zq, 0, 0)) goto done;
        print_wait(printer, pb);
    }
done:;
    char *rm[] = {"rm", "-rf", dir, 0};
    run(rm, 0, 0);
    return 0;
}

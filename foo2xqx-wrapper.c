// CUPS filter for the HP P1005: PDF/PS -> Ghostscript pbmraw -> foo2xqx at max density (-T5).
// argv: job user title copies options [file]; no file means stdin.
// shortcut: no PageSize table; gs takes the page size from the PDF/PS CUPS already sized, so a4 is only a fallback.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char tmpl[] = "/tmp/foo2xqx.XXXXXX";
    int tmp = mkstemp(tmpl), p[2], st1, st2;
    if (tmp < 0 || pipe(p) < 0) { perror("foo2xqx-wrapper"); return 1; }
    unlink(tmpl);
    pid_t gs = fork();
    if (gs == 0) {
        dup2(p[1], 1); close(p[0]); close(p[1]);
        execlp("gs", "gs", "-q", "-dBATCH", "-dSAFER", "-dQUIET", "-dNOPAUSE", "-sPAPERSIZE=a4",
               "-r1200x600", "-sDEVICE=pbmraw", "-sOutputFile=-", argc > 6 ? argv[6] : "-", (char *)0);
        _exit(127);
    }
    pid_t fo = fork();
    if (fo == 0) {
        dup2(p[0], 0); dup2(tmp, 1); close(p[0]); close(p[1]);
        execl("/usr/bin/foo2xqx", "foo2xqx", "-T5", "-r1200x600", "-p1", (char *)0);
        _exit(127);
    }
    close(p[0]); close(p[1]);
    waitpid(gs, &st1, 0); waitpid(fo, &st2, 0);
    // fail the job instead of sending a half-rendered one; CUPS ErrorPolicy retry-job re-runs it
    if (!WIFEXITED(st1) || WEXITSTATUS(st1) || !WIFEXITED(st2) || WEXITSTATUS(st2)) {
        fprintf(stderr, "ERROR: gs/foo2xqx failed (%d/%d)\n", st1, st2);
        return 1;
    }
    char buf[65536]; ssize_t n;
    lseek(tmp, 0, SEEK_SET);
    while ((n = read(tmp, buf, sizeof buf)) > 0)
        for (ssize_t o = 0, w; o < n; o += w)
            if ((w = write(1, buf + o, n - o)) < 0) return 1;
    return 0;
}

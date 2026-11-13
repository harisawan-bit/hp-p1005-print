// HP LaserJet P1005 self-heal. Rule 1: never open /dev/usb/lp0 while a job is active (that cut prints
// at page 5-7 in the old shell version). Rule 2: no job data before FWVER is verified.
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEV "/dev/usb/lp0"
#define FW "/var/lib/foo2zjs/firmware/sihpP1005.dl"
#define LOG "/var/log/printer-recover.log"
#define CHECK_EVERY 300

static void logf_(const char *m) {
    FILE *f = fopen(LOG, "a");
    if (!f) return;
    char t[32]; time_t n = time(0);
    strftime(t, sizeof t, "%F %T", localtime(&n));
    fprintf(f, "%s %s\n", t, m); fclose(f);
}

static int read_str(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(out, n, f) != NULL; fclose(f);
    if (ok) out[strcspn(out, "\n")] = 0;
    return ok;
}

// Prints the USB interface name (e.g. 2-1:1.0) and "bus-devnum" of the P1005, 0 if absent.
static int find_dev(char *iface, char *id) {
    DIR *d = opendir("/sys/bus/usb/devices");
    struct dirent *e; int found = 0;
    while (d && (e = readdir(d)))
        if (strstr(e->d_name, ":1.0")) {
            char p[512], v[32], pr[32], b[32], dn[32];
            snprintf(p, sizeof p, "/sys/bus/usb/devices/%s/../idVendor", e->d_name);
            if (!read_str(p, v, sizeof v) || strcmp(v, "03f0")) continue;
            snprintf(p, sizeof p, "/sys/bus/usb/devices/%s/../idProduct", e->d_name);
            if (!read_str(p, pr, sizeof pr) || strcmp(pr, "3d17")) continue;
            snprintf(p, sizeof p, "/sys/bus/usb/devices/%s/../busnum", e->d_name); read_str(p, b, sizeof b);
            snprintf(p, sizeof p, "/sys/bus/usb/devices/%s/../devnum", e->d_name); read_str(p, dn, sizeof dn);
            strcpy(iface, e->d_name); snprintf(id, 64, "%s-%s", b, dn); found = 1; break;
        }
    if (d) closedir(d);
    return found;
}

static int popen_has(const char *cmd, const char *needle) { // needle NULL: any output counts
    FILE *f = popen(cmd, "r");
    char line[512]; int hit = 0;
    while (f && fgets(line, sizeof line, f)) if (!needle || strstr(line, needle)) hit = 1;
    if (f) pclose(f);
    return hit;
}

static int device_held(void) { // some process has the printer node open
    DIR *p = opendir("/proc"); struct dirent *e; int held = 0;
    while (p && !held && (e = readdir(p))) {
        char dir[300], link[600], tgt[256]; struct dirent *f;
        snprintf(dir, sizeof dir, "/proc/%s/fd", e->d_name);
        DIR *fd = opendir(dir);
        while (fd && (f = readdir(fd))) {
            snprintf(link, sizeof link, "%s/%s", dir, f->d_name);
            ssize_t n = readlink(link, tgt, sizeof tgt - 1);
            if (n > 0 && (tgt[n] = 0, !strcmp(tgt, DEV))) { held = 1; break; }
        }
        if (fd) closedir(fd);
    }
    if (p) closedir(p);
    return held;
}

static int conn_9100(void) { // established connection to p910nd (port 9100 = 238C, state 01)
    FILE *f = fopen("/proc/net/tcp", "r"); char l[512]; int hit = 0;
    while (f && fgets(l, sizeof l, f)) {
        char st[8], loc[64];
        if (sscanf(l, " %*d: %63s %*s %7s", loc, st) == 2 && !strcmp(st, "01") && strstr(loc, ":238C")) hit = 1;
    }
    if (f) fclose(f);
    return hit;
}

static int busy(void) { return popen_has("lpstat -o 2>/dev/null", NULL) || device_held() || conn_9100(); }
static int fw_ok(void) { return access(DEV, F_OK) == 0 && popen_has("usb_printerid " DEV " 2>/dev/null", "FWVER"); }

static int upload_fw(void) {
    int in = open(FW, O_RDONLY), out = open(DEV, O_WRONLY); char b[8192]; ssize_t n; int ok = in >= 0 && out >= 0;
    while (ok && (n = read(in, b, sizeof b)) > 0) ok = write(out, b, n) == n;
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    return ok;
}

int main(void) {
    char iface[64], id[64], last_id[64] = "";
    int fails = 0; time_t last_check = 0;
    logf_("=== printer-recover (C) started ===");
    for (;;) {
        if (!find_dev(iface, id)) { last_id[0] = 0; system("systemctl is-active --quiet p910nd && systemctl stop p910nd"); sleep(10); continue; }
        if (access(DEV, F_OK)) {
            char c[512];
            system("modprobe usblp");
            snprintf(c, sizeof c, "echo %s > /sys/bus/usb/drivers/usblp/bind 2>/dev/null", iface); system(c);
            sleep(2);
        }
        if (busy()) { fails = 0; sleep(10); continue; }
        time_t now = time(0);
        if (!strcmp(id, last_id) && now - last_check < CHECK_EVERY) { sleep(10); continue; }
        strcpy(last_id, id); last_check = now;
        if (fw_ok()) {
            fails = 0;
            system("systemctl is-active --quiet p910nd || systemctl start p910nd");
            system("lpstat -p P1005 2>/dev/null | grep -q disabled && cupsenable P1005");
            continue;
        }
        logf_("fw check failed, printer idle");
        if (++fails < 2) { last_check = now - CHECK_EVERY + 20; continue; } // re-check in 20s
        if (busy()) continue;
        system("systemctl stop p910nd; cupsdisable P1005");
        logf_(upload_fw() ? "fw uploaded" : "WARN fw write failed");
        for (int i = 0; i < 10 && !fw_ok(); i++) sleep(1);
        if (fw_ok()) { logf_("fw VERIFIED"); system("systemctl start p910nd; cupsenable P1005"); fails = 0; }
        else logf_("WARN fw not verified, queue stays disabled");
    }
}

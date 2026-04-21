#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ── Report struct ───────────────────────────────────────────────────────── */

#define NAME_LEN     64
#define CATEGORY_LEN 32
#define DESC_LEN     128

/* All district folders live inside this directory */
#define DISTRICTS_DIR "districts"

typedef struct {
    int    id;
    char   inspector[NAME_LEN];
    double latitude;
    double longitude;
    char   category[CATEGORY_LEN];
    int    severity;       /* 1=minor  2=moderate  3=critical */
    time_t timestamp;
    char   description[DESC_LEN];
} Report;

/* ── District setup ──────────────────────────────────────────────────────── */
/*
 * File layout:
 *   districts/                750
 *   districts/<district>/     750
 *   districts/<district>/district.cfg      640
 *   districts/<district>/logged_district   644
 *   districts/<district>/reports.dat       664  (created on first add)
 */
void ensure_district(const char *district)
{
    struct stat st;
    char path[512];

    /* districts/ root folder */
    if (stat(DISTRICTS_DIR, &st) < 0) {
        if (mkdir(DISTRICTS_DIR, 0750) < 0) { perror("mkdir districts"); exit(1); }
    }
    chmod(DISTRICTS_DIR, 0750);

    /* districts/<district>/ */
    snprintf(path, sizeof(path), "%s/%s", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        if (mkdir(path, 0750) < 0) { perror("mkdir district"); exit(1); }
    }
    chmod(path, 0750);

    /* districts/<district>/district.cfg */
    snprintf(path, sizeof(path), "%s/%s/district.cfg", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0640);
        if (fd < 0) { perror("open district.cfg"); exit(1); }
        write(fd, "threshold=1\n", 12);
        close(fd);
    }
    chmod(path, 0640);

    /* districts/<district>/logged_district */
    snprintf(path, sizeof(path), "%s/%s/logged_district", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { perror("open logged_district"); exit(1); }
        close(fd);
    }
    chmod(path, 0644);

    /* reports.dat created on first --add */
}

/* ── Temporary main to test Step 1 ──────────────────────────────────────── */

int main(void)
{
    ensure_district("downtown");
    ensure_district("Balcescu");
    printf("Done. Check with:  ls -la districts/\n");
    return 0;
}
/*
 * scorer.c  –  Phase 3 helper
 *
 * Usage:  scorer <district_path>
 *
 * Reads all Report records from <district_path>/reports.dat and prints,
 * for each inspector found, the sum of severity levels (workload score).
 *
 * Output format (one line per inspector, printed to stdout):
 *   INSPECTOR:<name> SCORE:<total_severity>
 *
 * city_hub pipes this output back via dup2().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

/* Must match the Report struct in city_manager.c exactly */
#define NAME_LEN      64
#define CATEGORY_LEN  32
#define DESC_LEN      128

typedef struct {
    int    id;
    char   inspector[NAME_LEN];
    double latitude;
    double longitude;
    char   category[CATEGORY_LEN];
    int    severity;
    time_t timestamp;
    char   description[DESC_LEN];
} Report;

/* Simple dynamic table of (name, score) pairs */
#define MAX_INSPECTORS 256

typedef struct {
    char name[NAME_LEN];
    int  score;
} InspectorScore;

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: scorer <district_path>\n");
        exit(1);
    }

    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat", argv[1]);

    int fd = open(rpath, O_RDONLY);
    if (fd < 0) {
        /* No reports file — that is fine, just print nothing */
        exit(0);
    }

    InspectorScore table[MAX_INSPECTORS];
    int count = 0;

    Report r;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        /* Find existing entry for this inspector */
        int found = 0;
        for (int i = 0; i < count; i++) {
            if (strncmp(table[i].name, r.inspector, NAME_LEN) == 0) {
                table[i].score += r.severity;
                found = 1;
                break;
            }
        }
        if (!found && count < MAX_INSPECTORS) {
            strncpy(table[count].name, r.inspector, NAME_LEN - 1);
            table[count].name[NAME_LEN - 1] = '\0';
            table[count].score = r.severity;
            count++;
        }
    }
    close(fd);

    /* Print results — one line per inspector */
    for (int i = 0; i < count; i++) {
        printf("INSPECTOR:%s SCORE:%d\n", table[i].name, table[i].score);
    }

    return 0;
}

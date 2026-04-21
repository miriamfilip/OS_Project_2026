#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define NAME_LEN      64
#define CATEGORY_LEN  32
#define DESC_LEN      128
#define DISTRICTS_DIR "districts"

/* ── Report struct ───────────────────────────────────────────────────────── */

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

/* ── District setup ──────────────────────────────────────────────────────── */

void ensure_district(const char *district)
{
    struct stat st;
    char path[512];

    if (stat(DISTRICTS_DIR, &st) < 0) {
        if (mkdir(DISTRICTS_DIR, 0750) < 0) { perror("mkdir districts"); exit(1); }
    }
    chmod(DISTRICTS_DIR, 0750);

    snprintf(path, sizeof(path), "%s/%s", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        if (mkdir(path, 0750) < 0) { perror("mkdir district"); exit(1); }
    }
    chmod(path, 0750);

    snprintf(path, sizeof(path), "%s/%s/district.cfg", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0640);
        if (fd < 0) { perror("open district.cfg"); exit(1); }
        write(fd, "threshold=1\n", 12);
        close(fd);
    }
    chmod(path, 0640);

    snprintf(path, sizeof(path), "%s/%s/logged_district", DISTRICTS_DIR, district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { perror("open logged_district"); exit(1); }
        close(fd);
    }
    chmod(path, 0644);
}

/* ── Usage ───────────────────────────────────────────────────────────────── */

void usage(void)
{
    fprintf(stderr,
        "Usage:\n"
        "  city_manager --role <role> --user <user> --add <district>\n"
        "  city_manager --role <role> --user <user> --list <district>\n"
        "  city_manager --role <role> --user <user> --view <district> <id>\n"
        "  city_manager --role <role> --user <user> --remove_report <district> <id>\n"
        "  city_manager --role <role> --user <user> --update_threshold <district> <value>\n"
        "  city_manager --role <role> --user <user> --filter <district> <cond> [<cond>...]\n"
    );
    exit(1);
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *role     = NULL;
    const char *user     = NULL;
    const char *command  = NULL;
    const char *district = NULL;
    int         extra    = 0;   /* used for report_id or threshold value */

    /* --- parse arguments --- */
    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            role = argv[++i];

        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            user = argv[++i];

        } else if (strcmp(argv[i], "--add") == 0 && i + 1 < argc) {
            command  = "add";
            district = argv[++i];

        } else if (strcmp(argv[i], "--list") == 0 && i + 1 < argc) {
            command  = "list";
            district = argv[++i];

        } else if (strcmp(argv[i], "--view") == 0 && i + 2 < argc) {
            command  = "view";
            district = argv[++i];
            extra    = atoi(argv[++i]);

        } else if (strcmp(argv[i], "--remove_report") == 0 && i + 2 < argc) {
            command  = "remove_report";
            district = argv[++i];
            extra    = atoi(argv[++i]);

        } else if (strcmp(argv[i], "--update_threshold") == 0 && i + 2 < argc) {
            command  = "update_threshold";
            district = argv[++i];
            extra    = atoi(argv[++i]);

        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            command  = "filter";
            district = argv[++i];
            /* remaining argv after district are filter conditions — handled later */
        }
    }

    /* --- validate required arguments --- */
    if (!role || !user || !command || !district) {
        usage();
    }

    if (strcmp(role, "manager") != 0 && strcmp(role, "inspector") != 0) {
        fprintf(stderr, "ERROR: unknown role '%s'. Use 'manager' or 'inspector'.\n", role);
        exit(1);
    }

    /* --- temporary: just print what we parsed --- */
    printf("role     : %s\n", role);
    printf("user     : %s\n", user);
    printf("command  : %s\n", command);
    printf("district : %s\n", district);
    if (extra) printf("extra    : %d\n", extra);

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h> // open()
#include <unistd.h> //unix standard
#include <sys/stat.h> // struct stat define
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h> // for time_t


#define NAME_LEN      64 //for name
#define CATEGORY_LEN  32 //"flooding" "road"
#define DESC_LEN      128 // description
#define PID_FILE      ".monitor_pid"


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

// Setup for districts  

void ensure_district(const char *district) //building the district
{
    struct stat st; // buffer for innode info???
    char path[512];

    snprintf(path, sizeof(path), "%s", district);
    if (stat(path, &st) < 0) {
        if (mkdir(path, 0750) < 0) { perror("mkdir district"); exit(1); }
    }
    chmod(path, 0750);

    snprintf(path, sizeof(path), "%s/reports.dat", district);
    if(stat(path, &st)<0){
        int fd = open(path, O_WRONLY | O_CREAT, 0664);
        if (fd < 0) { perror("open logged_district"); exit(1); }
        close(fd);
        chmod(path, 0664); // report.dat
    }    
    

    snprintf(path, sizeof(path), "%s/district.cfg", district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0640);
        if (fd < 0) { perror("open district.cfg"); exit(1); }
        write(fd, "threshold=1\n", 12);
        close(fd);
    }
    chmod(path, 0640); // district.cfg

    
    snprintf(path, sizeof(path), "%s/logged_district",  district);
    if (stat(path, &st) < 0) {
        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { perror("open logged_district"); exit(1); }
        close(fd);
    }
    chmod(path, 0644); //logged_district
}

// Log an action 
void log_action(const char *district, const char *role, const char *user, const char *action)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/logged_district",  district);

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return; 
    chmod(path, 0644); 

    char buf[256];
    time_t now = time(NULL);
    snprintf(buf, sizeof(buf), "%ld\t%s\t%s\t%s\n", (long)now, user, role, action);
    write(fd, buf, strlen(buf));
    close(fd);
}

// Permission bits to string 
void mode_to_str(mode_t mode, char out[10])
{
    out[0] = (mode & S_IRUSR) ? 'r' : '-';
    out[1] = (mode & S_IWUSR) ? 'w' : '-';
    out[2] = (mode & S_IXUSR) ? 'x' : '-'; //user
    out[3] = (mode & S_IRGRP) ? 'r' : '-';
    out[4] = (mode & S_IWGRP) ? 'w' : '-';
    out[5] = (mode & S_IXGRP) ? 'x' : '-'; //group
    out[6] = (mode & S_IROTH) ? 'r' : '-';
    out[7] = (mode & S_IWOTH) ? 'w' : '-';
    out[8] = (mode & S_IXOTH) ? 'x' : '-'; //other
    out[9] = '\0';
}

// Permission check 
int check_permission(const char *path, const char *role, int need_read, int need_write)
{
    struct stat st;
    if (stat(path, &st) < 0) return 1; /* file doesn't exist yet, allow creation */

    mode_t m = st.st_mode;
    //110110100 reports.dat
    int ok = 1;

    if (strcmp(role, "manager") == 0) {
        if (need_read  && !(m & S_IRUSR)) ok = 0;
        if (need_write && !(m & S_IWUSR)) ok = 0;
    } else {
        if (need_read  && !(m & S_IRGRP)) ok = 0;
        if (need_write && !(m & S_IWGRP)) ok = 0;
    }

    if (!ok) {
        char sym[10];
        mode_to_str(m, sym);
        fprintf(stderr, "ERROR: role '%s' does not have %s%s access on '%s' (perms: %s)\n",
                role,
                need_read  ? "read"   : "",
                need_write ? "/write" : "",
                path, sym);
    }
    return ok;
}

// Symlink helpers 
void update_symlink(const char *district)
{
    char link_name[512];
    char target[512];
    // char abs_target[1024];  // large enough for cwd + "/" + target
    // char cwd[512];          // match target size

    snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
    snprintf(target, sizeof(target), "%s/reports.dat", district);
    //printf("%s ", target);

    // if (realpath(target, abs_target) == NULL) {
    //     getcwd(cwd, sizeof(cwd));
    //     snprintf(abs_target, sizeof(abs_target), "%s/%s", cwd, target);
    // }
    // printf("%s ", abs_target);

    struct stat lst;
    if (lstat(link_name, &lst) == 0){
         unlink(link_name);
    }
    symlink(target, link_name);
}

void print_report(const Report *r)
{
    char tsbuf[64];
    struct tm *tm = localtime(&r->timestamp);
    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", tm);

    printf("  ID        : %d\n",           r->id);
    printf("  Inspector : %s\n",           r->inspector);
    printf("  Location  : (%.4f, %.4f)\n", r->latitude, r->longitude);
    printf("  Category  : %s\n",           r->category);
    printf("  Severity  : %d\n",           r->severity);
    printf("  Timestamp : %s\n",           tsbuf);
    printf("  Desc      : %s\n",           r->description);
}

// Commands

//add
void cmd_add(const char *district, const char *role, const char *user)
{
    ensure_district(district);

    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat",  district);

    if (!check_permission(rpath, role, 0, 1)) exit(1); //both may add reports

    Report r;
    memset(&r, 0, sizeof(r));

    struct stat st;
    if(stat(rpath, &st) == 0){
        r.id=(int)(st.st_size / sizeof(Report)) + 1;
    }
    else{
        r.id=1;
    }

    strncpy(r.inspector, user, NAME_LEN - 1);
    r.timestamp = time(NULL);

    printf("Latitude : ");  scanf("%lf", &r.latitude);
    printf("Longitude: ");  scanf("%lf", &r.longitude);
    printf("Category (road/lighting/flooding/other): "); scanf("%31s", r.category);
    printf("Severity (1=minor 2=moderate 3=critical): "); scanf("%d", &r.severity);

    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    printf("Description: ");
    fgets(r.description, DESC_LEN, stdin);
    size_t len = strlen(r.description);
    if (len > 0 && r.description[len - 1] == '\n')
        r.description[len - 1] = '\0';

    
    int fd = open(rpath, O_WRONLY | O_CREAT | O_APPEND, 0664);
    if (fd < 0) { perror("open reports.dat"); exit(1); }
    chmod(rpath, 0664);  // reports.dat permissions
    write(fd, &r, sizeof(r));
    close(fd);

    update_symlink(district);
    printf("Report %d added to district '%s'.\n", r.id, district);

    /// monitor process notify
    int monitor_pid = -1;
    int pid_fd = open(PID_FILE, O_RDONLY);
    int notified = 0;

    if (pid_fd >= 0) {
        char pid_buf[32] = {0};
        int bytes_read = read(pid_fd, pid_buf, sizeof(pid_buf) - 1);
        if (bytes_read > 0) {
            monitor_pid = atoi(pid_buf);
            if (monitor_pid > 0 && kill(monitor_pid, SIGUSR1) == 0) {
                notified = 1;
            }
        }
        close(pid_fd);
    }

    char action_log[128];
    if (notified) {
        snprintf(action_log, sizeof(action_log), "add (monitor notified successfully)");
    } else {
        snprintf(action_log, sizeof(action_log), "add (monitor could not be informed)");
    }
    log_action(district, role, user, action_log);
}

// remove district
void cmd_remove_district(const char *district, const char *role, const char *user)
{
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr,"ERROR: only managers can remove districts.\n");
        exit(1);
    }

    char target_dir[512];
    snprintf(target_dir, sizeof(target_dir), "%s", district);

    struct stat st;
    if (stat(target_dir, &st) < 0) {
        fprintf(stderr,"ERROR: District '%s' does not exist.\n", district);
        exit(1);
    }

    pid_t pid = fork(); // child- parent process
    if (pid < 0) {
        perror("fork"); 
        exit(1);
    } else if (pid == 0) {
        execlp("rm", "rm", "-rf", target_dir, NULL);
        perror("execlp");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("District '%s' and all its contents have been removed.\n", district);
            
            char link_name[512];
            snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
            unlink(link_name);
        } else {
            fprintf(stderr, "ERROR: Failed to remove district '%s'.\n", district);
        }
    }
}

// list
void cmd_list(const char *district, const char *role, const char *user)
{
    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat",  district);

    if (!check_permission(rpath, role, 1, 0)) exit(1);

    struct stat st;
    if (stat(rpath, &st) < 0) {
        printf("No reports found for district '%s'.\n", district);
        return;
    }

    char sym[10];
    mode_to_str(st.st_mode, sym);
    // printf("\n%d\n",st.st_mode);
    char tmbuf[64];
    struct tm *tm = localtime(&st.st_mtime);
    strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", tm);
    printf("reports.dat | perms: %s | size: %lld bytes | modified: %s\n",
           sym, (long long)st.st_size, tmbuf);
    printf("---\n");

    int fd = open(rpath, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); exit(1); }

    Report r;
    int count = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        printf("--- Report %d ---\n", r.id);
        print_report(&r);
        count++;
    }
    close(fd);

    if (count == 0) printf("(no reports)\n");
    log_action(district, role, user, "list");
}

// view
void cmd_view(const char *district, const char *role, const char *user, int report_id)
{
    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat",  district);

    if (!check_permission(rpath, role, 1, 0)) exit(1);

    int fd = open(rpath, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); exit(1); }

    Report r;
    int found = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.id == report_id) {
            print_report(&r);
            found = 1;
            break;
        }
    }
    close(fd);

    if (!found) fprintf(stderr, "Report %d not found in district '%s'.\n", report_id, district);
    log_action(district, role, user, "view");
}

// remove report
void cmd_remove_report(const char *district, const char *role, const char *user, int report_id)
{
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr, "ERROR: only managers can remove reports.\n");
        exit(1);
    }

    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat",  district);

    if (!check_permission(rpath, role, 1, 1)) exit(1);

    struct stat st;
    if (stat(rpath, &st) < 0) { fprintf(stderr, "No reports found.\n"); exit(1); }
    int total = (int)(st.st_size / sizeof(Report));

    Report *records = malloc(total * sizeof(Report));
    if (!records) { perror("malloc"); exit(1); }

    int fd = open(rpath, O_RDWR);
    if (fd < 0) { perror("open reports.dat"); free(records); exit(1); }
    read(fd, records, total * sizeof(Report));

    int found = 0;
    for (int i = 0; i < total; i++) {
        if (records[i].id == report_id) {
            found = 1;
            lseek(fd, (off_t)i * sizeof(Report), SEEK_SET);
            for (int j = i + 1; j < total; j++) {
                write(fd, &records[j], sizeof(Report));
            }
            ftruncate(fd, (off_t)(total - 1) * sizeof(Report));
            break;
        }
    }

    free(records);
    close(fd);

    if (!found) {
        fprintf(stderr, "Report %d not found in district '%s'.\n", report_id, district);
    } else {
        printf("Report %d removed from district '%s'.\n", report_id, district);
        log_action(district, role, user, "remove_report");
    }
}

// update threshold
void cmd_update_threshold(const char *district, const char *role,
                           const char *user, int value)
{
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr, "ERROR: only managers can update the threshold.\n");
        exit(1);
    }

    ensure_district(district);

    char cfg[512];
    snprintf(cfg, sizeof(cfg), "%s/district.cfg", district);
    //110100000
    struct stat st;
    if (stat(cfg, &st) == 0) {
        mode_t perms = st.st_mode & 0777;
        if (perms != 0640) {
            char sym[10];
            mode_to_str(st.st_mode, sym);
            fprintf(stderr,
                "ERROR: district.cfg permissions are %s (expected rw-r-----). Refusing.\n",
                sym);
            exit(1);
        }
    }

    if (!check_permission(cfg, role, 0, 1)) exit(1);

    int fd = open(cfg, O_WRONLY | O_TRUNC | O_CREAT, 0640); //creat
    if (fd < 0) { perror("open district.cfg"); exit(1); }
    chmod(cfg, 0640);

    char buf[64];
    snprintf(buf, sizeof(buf), "threshold=%d\n", value);
    write(fd, buf, strlen(buf));
    close(fd);

    printf("Threshold for district '%s' updated to %d.\n", district, value);
    log_action(district, role, user, "update_threshold");
}

/* ── filter (AI-assisted functions) ─────────────────────────────────────── */
int parse_condition(const char *input, char *field, char *op, char *value)
{
    char buf[256];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;

    char *colon1 = strchr(p, ':');
    if (!colon1) return 0;
    *colon1 = '\0';
    strncpy(field, p, 63); field[63] = '\0';
    p = colon1 + 1;

    char *colon2 = strchr(p, ':');
    if (!colon2) return 0;
    *colon2 = '\0';
    strncpy(op, p, 7); op[7] = '\0';
    p = colon2 + 1;

    strncpy(value, p, 127); value[127] = '\0';

    return 1;
}

int match_condition(Report *r, const char *field, const char *op, const char *value)
{
    if (strcmp(field, "severity") == 0) {
        int val = atoi(value);
        int sev = r->severity;
        if (strcmp(op, "==") == 0) return sev == val;
        if (strcmp(op, "!=") == 0) return sev != val;
        if (strcmp(op, "<")  == 0) return sev <  val;
        if (strcmp(op, "<=") == 0) return sev <= val;
        if (strcmp(op, ">")  == 0) return sev >  val;
        if (strcmp(op, ">=") == 0) return sev >= val;

    } else if (strcmp(field, "category") == 0) {
        int cmp = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;

    } else if (strcmp(field, "inspector") == 0) {
        int cmp = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;

    } else if (strcmp(field, "timestamp") == 0) {
        time_t val = (time_t)atol(value);
        time_t ts  = r->timestamp;
        if (strcmp(op, "==") == 0) return ts == val;
        if (strcmp(op, "!=") == 0) return ts != val;
        if (strcmp(op, "<")  == 0) return ts <  val;
        if (strcmp(op, "<=") == 0) return ts <= val;
        if (strcmp(op, ">")  == 0) return ts >  val;
        if (strcmp(op, ">=") == 0) return ts >= val;
    }

    return 0;
}

// filter
void cmd_filter(const char *district, const char *role, const char *user,
                int cond_count, char **conditions)
{
    char rpath[512];
    snprintf(rpath, sizeof(rpath), "%s/reports.dat", district);

    if (!check_permission(rpath, role, 1, 0)) exit(1);

    char link_name[512];
    snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
    struct stat lst;
    if (lstat(link_name, &lst) == 0 && S_ISLNK(lst.st_mode)) {
        struct stat fst;
        if (stat(link_name, &fst) < 0)
            fprintf(stderr, "WARNING: symlink '%s' is dangling.\n", link_name);
    }

    char fields[8][64], ops[8][8], values[8][128];
    int nconds = 0;
    for (int i = 0; i < cond_count && nconds < 8; i++) {
        if (parse_condition(conditions[i], fields[nconds], ops[nconds], values[nconds]))
            nconds++;
        else
            fprintf(stderr, "WARNING: could not parse condition '%s', skipping.\n",
                    conditions[i]);
    }

    int fd = open(rpath, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); exit(1); }

    Report r;
    int found = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        int match = 1;
        for (int i = 0; i < nconds; i++) {
            if (!match_condition(&r, fields[i], ops[i], values[i])) {
                match = 0;
                break;
            }
        }
        if (match) {
            printf("--- Report %d ---\n", r.id);
            print_report(&r);
            found++;
        }
    }
    close(fd);

    if (!found) printf("No reports matched the given conditions.\n");
    log_action(district, role, user, "filter");
}

// usage when format for input not correct
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
        "  city_manager --role <role> --user <user> --remove_district <district>\n"
    );
    exit(1);
}

int main(int argc, char *argv[])
{
    const char *role     = NULL;
    const char *user     = NULL;
    const char *command  = NULL;
    const char *district = NULL;
    int extra = 0;
    int filter_start = 0;
    int filter_count = 0;

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
        } else if (strcmp(argv[i], "--remove_district") == 0 && i + 1 < argc) {
            command  = "remove_district";
            district = argv[++i];
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            command      = "filter";
            district     = argv[++i];
            filter_start = i + 1;
            filter_count = argc - filter_start;
            i = argc; 
        }
    }

    if (!role || !user || !command || !district) usage();

    if (strcmp(role, "manager") != 0 && strcmp(role, "inspector") != 0) {
        fprintf(stderr, "ERROR: unknown role '%s'. Use 'manager' or 'inspector'.\n", role);
        exit(1);
    }

    if      (strcmp(command, "add")              == 0) cmd_add(district, role, user);
    else if (strcmp(command, "list")             == 0) cmd_list(district, role, user);
    else if (strcmp(command, "view")             == 0) cmd_view(district, role, user, extra);
    else if (strcmp(command, "remove_report")    == 0) cmd_remove_report(district, role, user, extra);
    else if (strcmp(command, "update_threshold") == 0) cmd_update_threshold(district, role, user, extra);
    else if (strcmp(command, "remove_district")  == 0) cmd_remove_district(district, role, user);
    else if (strcmp(command, "filter")           == 0) cmd_filter(district, role, user, filter_count, argv + filter_start);

    return 0;
}
/*
 * city_hub.c  –  Phase 3
 *
 * Interactive command-line hub.  Understands two commands:
 *
 *   start_monitor
 *       Creates a background child process (hub_mon).  hub_mon in turn forks
 *       the monitor_reports executable.  Before forking the monitor, hub_mon
 *       creates a pipe so that the monitor's stdout is redirected into the
 *       pipe.  hub_mon reads from the read end and relays the monitor's
 *       messages to the user.  If the monitor reports it is already running
 *       (ERROR: prefix) or that it has stopped (STOP: prefix), hub_mon prints
 *       a specific message and exits.
 *
 *   calculate_scores <district1> [<district2> ...]
 *       For each district in the list, city_hub forks a scorer process.
 *       dup2() is used to redirect each scorer's stdout into a pipe.
 *       city_hub collects and prints a combined workload report.
 *
 *   quit / exit
 *       Exit city_hub.
 *
 * System calls used: pipe(), fork(), dup2(), exec*(), read(), write(),
 *                    waitpid(), open(), close()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define DISTRICTS_DIR "districts"
#define MONITOR_BIN   "./monitor_reports"
#define SCORER_BIN    "./scorer"
#define MAX_LINE      1024
#define MAX_ARGS      64

/* ════════════════════════════════════════════════════════════════════════════
   start_monitor
   ════════════════════════════════════════════════════════════════════════════ */

/*
 * hub_mon_run() — this function is called inside the hub_mon child process.
 * It sets up a pipe, forks monitor_reports, wires monitor's stdout to the
 * pipe write-end, then reads and prints all messages from the read end.
 */
static void hub_mon_run(void)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("[hub_mon] pipe");
        exit(1);
    }

    pid_t mon_pid = fork();
    if (mon_pid < 0) {
        perror("[hub_mon] fork monitor");
        exit(1);
    }

    if (mon_pid == 0) {
        /* ── monitor child ── */
        /* Redirect stdout → pipe write end */
        close(pipefd[0]);                      /* close read end  */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2 monitor stdout");
            exit(1);
        }
        close(pipefd[1]);                      /* original write end no longer needed */

        execl(MONITOR_BIN, MONITOR_BIN, NULL);
        perror("execl monitor_reports");
        exit(1);
    }

    /* ── hub_mon parent ── */
    close(pipefd[1]); /* close write end — only monitor writes */

    /*
     * Read structured messages line by line from the pipe.
     * Format: TYPE:message text\n
     */
    char buf[MAX_LINE];
    int  buf_pos = 0;
    char ch;
    int  monitor_done = 0;

    while (!monitor_done) {
        ssize_t n = read(pipefd[0], &ch, 1);
        if (n <= 0) {
            /* Monitor closed the pipe (exited) */
            printf("[hub] Monitor pipe closed unexpectedly.\n");
            fflush(stdout);
            break;
        }

        if (ch == '\n' || buf_pos >= MAX_LINE - 1) {
            buf[buf_pos] = '\0';
            buf_pos = 0;

            /* Dispatch on message type */
            if (strncmp(buf, "INFO:", 5) == 0) {
                printf("[monitor] %s\n", buf + 5);
                fflush(stdout);

            } else if (strncmp(buf, "REPORT:", 7) == 0) {
                printf("[monitor] EVENT: %s\n", buf + 7);
                fflush(stdout);

            } else if (strncmp(buf, "STOP:", 5) == 0) {
                printf("[monitor] STOPPED: %s\n", buf + 5);
                printf("[hub] Monitor has ended.\n");
                fflush(stdout);
                monitor_done = 1;

            } else if (strncmp(buf, "ERROR:", 6) == 0) {
                printf("[monitor] ERROR: %s\n", buf + 6);
                printf("[hub] Monitor could not start.\n");
                fflush(stdout);
                monitor_done = 1;

            } else if (strlen(buf) > 0) {
                /* Fallback — print raw message */
                printf("[monitor] %s\n", buf);
                fflush(stdout);
            }
        } else {
            buf[buf_pos++] = ch;
        }
    }

    close(pipefd[0]);
    /* Reap the monitor process */
    waitpid(mon_pid, NULL, 0);
    exit(0);
}

/*
 * cmd_start_monitor() — called from city_hub's main loop.
 * Forks hub_mon as a background process and returns immediately.
 */
static void cmd_start_monitor(void)
{
    pid_t hub_mon_pid = fork();
    if (hub_mon_pid < 0) {
        perror("fork hub_mon");
        return;
    }
    if (hub_mon_pid == 0) {
        /* hub_mon child */
        hub_mon_run();  /* never returns */
        exit(0);
    }
    /* Parent returns immediately — hub_mon runs in background */
    printf("[hub] Monitor background process started (hub_mon PID %d).\n",
           (int)hub_mon_pid);
}

/* ════════════════════════════════════════════════════════════════════════════
   calculate_scores
   ════════════════════════════════════════════════════════════════════════════ */

/*
 * For each district in the argument list, fork a scorer process, redirect its
 * stdout into a pipe with dup2(), collect all output, and print a combined
 * workload report.
 */
static void cmd_calculate_scores(int district_count, char **districts)
{
    if (district_count == 0) {
        printf("[hub] calculate_scores: no districts specified.\n");
        return;
    }

    printf("[hub] Workload report:\n");
    printf("%-20s %-20s %s\n", "District", "Inspector", "Score");
    printf("%-20s %-20s %s\n", "--------", "---------", "-----");

    for (int d = 0; d < district_count; d++) {
        const char *district = districts[d];

        /* Build path: districts/<district> */
        char district_path[512];
        snprintf(district_path, sizeof(district_path), "%s/%s", DISTRICTS_DIR, district);

        /* Check the district exists */
        struct stat st;
        if (stat(district_path, &st) < 0) {
            printf("[hub] District '%s' not found — skipping.\n", district);
            continue;
        }

        /* Create a pipe for the scorer's stdout */
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            perror("pipe");
            continue;
        }

        pid_t scorer_pid = fork();
        if (scorer_pid < 0) {
            perror("fork scorer");
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (scorer_pid == 0) {
            /* ── scorer child ── */
            close(pipefd[0]);                      /* close read end */
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                perror("dup2 scorer stdout");
                exit(1);
            }
            close(pipefd[1]);
            execl(SCORER_BIN, SCORER_BIN, district_path, NULL);
            perror("execl scorer");
            exit(1);
        }

        /* ── hub parent ── */
        close(pipefd[1]); /* close write end */

        /* Read all output from the scorer */
        char line[MAX_LINE];
        int  line_pos = 0;
        char ch;
        int  any_output = 0;

        while (read(pipefd[0], &ch, 1) == 1) {
            if (ch == '\n' || line_pos >= MAX_LINE - 1) {
                line[line_pos] = '\0';
                line_pos = 0;

                /*
                 * Scorer output format:
                 *   INSPECTOR:<name> SCORE:<total>
                 */
                if (strncmp(line, "INSPECTOR:", 10) == 0) {
                    char inspector[64] = {0};
                    int  score = 0;
                    /* Parse: INSPECTOR:<name> SCORE:<n> */
                    char *score_ptr = strstr(line, " SCORE:");
                    if (score_ptr) {
                        int name_len = (int)(score_ptr - line - 10); /* 10 = len("INSPECTOR:") */
                        if (name_len > 0 && name_len < 63) {
                            strncpy(inspector, line + 10, name_len);
                            inspector[name_len] = '\0';
                        }
                        score = atoi(score_ptr + 7); /* 7 = len(" SCORE:") */
                    }
                    printf("%-20s %-20s %d\n", district, inspector, score);
                    any_output = 1;
                }
            } else {
                line[line_pos++] = ch;
            }
        }

        close(pipefd[0]);
        waitpid(scorer_pid, NULL, 0);

        if (!any_output) {
            printf("%-20s (no reports)\n", district);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════════
   Main interactive loop
   ════════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    /* Avoid zombie processes from hub_mon children finishing in background */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = SIG_DFL;   /* default: let waitpid() reap them */
    sa_chld.sa_flags   = SA_NOCLDWAIT; /* auto-reap so we don't get zombies */
    sigaction(SIGCHLD, &sa_chld, NULL);

    printf("=== city_hub ===  (Phase 3)\n");
    printf("Commands: start_monitor | calculate_scores <district...> | quit\n\n");

    char line[MAX_LINE];

    while (1) {
        printf("hub> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF (Ctrl-D) */
            printf("\n[hub] EOF — exiting.\n");
            break;
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        /* Tokenise */
        char *argv[MAX_ARGS];
        int   argc = 0;
        char *token = strtok(line, " \t");
        while (token && argc < MAX_ARGS - 1) {
            argv[argc++] = token;
            token = strtok(NULL, " \t");
        }
        argv[argc] = NULL;

        if (argc == 0) continue;

        if (strcmp(argv[0], "quit") == 0 || strcmp(argv[0], "exit") == 0) {
            printf("[hub] Exiting.\n");
            break;

        } else if (strcmp(argv[0], "start_monitor") == 0) {
            cmd_start_monitor();

        } else if (strcmp(argv[0], "calculate_scores") == 0) {
            cmd_calculate_scores(argc - 1, argv + 1);

        } else {
            printf("[hub] Unknown command: '%s'\n", argv[0]);
            printf("      Available: start_monitor | calculate_scores <district...> | quit\n");
        }
    }

    return 0;
}

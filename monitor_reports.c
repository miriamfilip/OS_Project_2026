#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define PID_FILE ".monitor_pid"

volatile sig_atomic_t keep_running = 1;

/* ── Signal Handlers ─────────────────────────────────────────────────────── */

void handle_sigint(int sig)
{
    /* Tell the main loop to exit */
    keep_running = 0;
    
    /* Safely write to stdout inside a signal handler */
    const char *msg = "\n[monitor] SIGINT received. Shutting down...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

void handle_sigusr1(int sig)
{
    /* Safely write to stdout inside a signal handler */
    const char *msg = "[monitor] SIGUSR1 received: A new report has been added.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

/* ── Write PID to .monitor_pid ───────────────────────────────────────────── */
void write_pid_file(void)
{
    /* O_TRUNC overwrites the file if it already exists */
    int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open .monitor_pid");
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", getpid());
    write(fd, buf, strlen(buf));
    close(fd);
    printf("[monitor] Started. PID %d written to %s\n", getpid(), PID_FILE);
}

/* ── Delete .monitor_pid on exit ─────────────────────────────────────────── */
void delete_pid_file(void)
{
    unlink(PID_FILE);
    printf("[monitor] PID file removed. Goodbye.\n");
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    /* Set up sigaction for SIGINT */
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(1);
    }

    /* Set up sigaction for SIGUSR1 */
    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        perror("sigaction SIGUSR1");
        exit(1);
    }

    write_pid_file();

    printf("[monitor] Running. Press Ctrl+C to stop.\n");
    
    /* keep_running is changed to 0 when SIGINT is caught */
    while (keep_running) {
        pause(); /* sleep until a signal arrives */
    }

    delete_pid_file();
    return 0;
}
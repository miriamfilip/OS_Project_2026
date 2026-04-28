#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define PID_FILE ".monitor_pid"

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
    write_pid_file();

    /* keep running — signal handlers will be added in next steps */
    printf("[monitor] Running. Press Ctrl+C to stop.\n");
    while (1) {
        pause(); /* sleep until a signal arrives */
    }

    delete_pid_file();
    return 0;
}

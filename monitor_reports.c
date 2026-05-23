#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define PID_FILE ".monitor_pid"


volatile sig_atomic_t keep_running = 1;


void handle_sigint(int sig)
{
    (void)sig;
    keep_running = 0;
    /* Signal-safe; hub_mon reads "STOP:" prefix to know monitor ended */
    const char *msg = "STOP:SIGINT received. Shutting down.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

void handle_sigusr1(int sig)
{
    (void)sig;
    const char *msg = "REPORT:A new report has been added.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

void write_pid_file(void)
{
    int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        const char *msg = "ERROR:Could not create .monitor_pid file.\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        exit(1);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", getpid());
    write(fd, buf, strlen(buf));
    close(fd);
}


void delete_pid_file(void)
{
    unlink(PID_FILE);
}

/
int main(void)
{
    
    int pid_fd = open(PID_FILE, O_RDONLY);
    if (pid_fd >= 0) {
        char buf[32] = {0};
        int n = read(pid_fd, buf, sizeof(buf) - 1);
        close(pid_fd);
        if (n > 0) {
            pid_t existing = (pid_t)atoi(buf);
            if (existing > 0 && kill(existing, 0) == 0) {
                char errmsg[128];
                snprintf(errmsg, sizeof(errmsg),
                         "ERROR:Monitor already running with PID %d.\n",
                         (int)existing);
                write(STDOUT_FILENO, errmsg, strlen(errmsg));
                exit(1);
            }
        }
    }

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        const char *msg = "ERROR:sigaction SIGINT failed.\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        exit(1);
    }

    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
        const char *msg = "ERROR:sigaction SIGUSR1 failed.\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        exit(1);
    }

    write_pid_file();

    char startmsg[64];
    snprintf(startmsg, sizeof(startmsg), "INFO:Monitor started. PID %d.\n", getpid());
    write(STDOUT_FILENO, startmsg, strlen(startmsg));

    while (keep_running) {
        pause(); /* sleep until a signal arrives */
    }

    delete_pid_file();
    /* STOP message already written from inside handle_sigint */
    return 0;
}
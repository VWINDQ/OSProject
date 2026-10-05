#define _POSIX_C_SOURCE 200809L

#include "utils/server_lock.h"

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define LOCK_PATH "/tmp/cinema_lock_test.lock"

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

int main(void)
{
    int ready[2];
    pid_t child;
    char signal_byte;
    int status;

    CHECK(pipe(ready) == 0);
    child = fork();
    if (child == 0) {
        close(ready[0]);
        if (server_lock_acquire_at(LOCK_PATH) != 0 ||
            write(ready[1], "x", 1) != 1) {
            _exit(1);
        }
        pause();
        _exit(0);
    }

    close(ready[1]);
    CHECK(read(ready[0], &signal_byte, 1) == 1);
    CHECK(server_lock_acquire_at(LOCK_PATH) == -1);

    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    CHECK(server_lock_acquire_at(LOCK_PATH) == 0);

    unlink(LOCK_PATH);
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("server lock tests passed\n");
    return 0;
}

#define _POSIX_C_SOURCE 200809L

#include "utils/server_lock.h"

#include "constants/constants.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int server_lock_acquire_at(const char *path)
{
    struct flock lock;
    int descriptor = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);

    if (descriptor == -1) {
        fprintf(stderr, "Cannot open server lock file %s: %s\n", path,
                strerror(errno));
        return -1;
    }

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(descriptor, F_SETLK, &lock) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            fprintf(stderr,
                    "Another server is already running (lock file %s is in use).\n",
                    path);
        } else {
            fprintf(stderr, "Cannot lock %s: %s\n", path, strerror(errno));
        }
        close(descriptor);
        return -1;
    }

    /* The descriptor stays open on purpose: closing it would drop the lock. */
    return 0;
}

int server_lock_acquire(void)
{
    return server_lock_acquire_at(SERVER_LOCK_FILE);
}

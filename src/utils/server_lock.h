#ifndef CINEMA_SERVER_LOCK_H
#define CINEMA_SERVER_LOCK_H

/* Takes an exclusive lock that lasts until the process ends, so only one
 * server runs at a time. Returns 0, or -1 (after printing why) when another
 * process already holds it. The operating system drops the lock when the
 * holder dies, even after kill -9. */
int server_lock_acquire(void);
int server_lock_acquire_at(const char *path);

#endif

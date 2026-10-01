#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <time.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_out = NULL;
static unsigned long log_sequence = 0;
static struct timespec log_start;

void logger_init(FILE *out)
{
    pthread_mutex_lock(&log_mutex);
    log_out = out;
    log_sequence = 0;
    clock_gettime(CLOCK_MONOTONIC, &log_start);
    pthread_mutex_unlock(&log_mutex);
}

void log_event(const char *actor, const char *format, ...)
{
    struct timespec now;
    va_list arguments;
    long elapsed_ms;

    pthread_mutex_lock(&log_mutex);
    if (log_out != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (long)(now.tv_sec - log_start.tv_sec) * 1000L +
                     (now.tv_nsec - log_start.tv_nsec) / 1000000L;
        fprintf(log_out, "[#%04lu +%04ldms][%s] ", ++log_sequence, elapsed_ms,
                actor);
        va_start(arguments, format);
        vfprintf(log_out, format, arguments);
        va_end(arguments);
        fputc('\n', log_out);
        fflush(log_out);
    }
    pthread_mutex_unlock(&log_mutex);
}

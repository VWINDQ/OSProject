#define _POSIX_C_SOURCE 200809L

#include "utils/logger.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

#define THREADS 4
#define LINES_PER_THREAD 50

static void test_log_before_init_is_ignored(void)
{
    log_event("Server", "nobody is listening %d", 1);
    CHECK(1);
}

static void test_format(void)
{
    FILE *log = tmpfile();
    char line[256];

    CHECK(log != NULL);
    logger_init(log);
    log_event("Worker-1", "hello %d", 5);
    rewind(log);
    CHECK(fgets(line, sizeof(line), log) != NULL);
    CHECK(strncmp(line, "[#0001 +", 8) == 0);
    CHECK(strstr(line, "ms][Worker-1] hello 5\n") != NULL);
    logger_init(NULL);
    fclose(log);
}

static void *writer(void *argument)
{
    int id = *(int *)argument;
    char actor[32];
    int index;

    snprintf(actor, sizeof(actor), "Worker-%d", id);
    for (index = 0; index < LINES_PER_THREAD; ++index) {
        log_event(actor, "line %d", index);
    }
    return NULL;
}

static void test_sequence_numbers_and_whole_lines(void)
{
    FILE *log = tmpfile();
    pthread_t threads[THREADS];
    int ids[THREADS];
    int next_line[THREADS + 1] = {0};
    char line[256];
    unsigned long sequence;
    unsigned long expected = 1;
    int worker;
    int number;
    int index;

    CHECK(log != NULL);
    logger_init(log);
    for (index = 0; index < THREADS; ++index) {
        ids[index] = index + 1;
        CHECK(pthread_create(&threads[index], NULL, writer, &ids[index]) == 0);
    }
    for (index = 0; index < THREADS; ++index) {
        pthread_join(threads[index], NULL);
    }

    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        CHECK(sscanf(line, "[#%lu +%*dms][Worker-%d] line %d", &sequence, &worker,
                     &number) == 3);
        CHECK(sequence == expected);
        ++expected;
        if (worker >= 1 && worker <= THREADS) {
            CHECK(number == next_line[worker]);
            ++next_line[worker];
        }
    }
    CHECK(expected == (unsigned long)(THREADS * LINES_PER_THREAD) + 1);
    logger_init(NULL);
    fclose(log);
}

int main(void)
{
    test_log_before_init_is_ignored();
    test_format();
    test_sequence_numbers_and_whole_lines();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("logger tests passed\n");
    return 0;
}

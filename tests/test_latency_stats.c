#include "benchmark/latency_stats.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void test_percentiles_use_nearest_rank(void)
{
    long values[100];
    size_t index;

    for (index = 0; index < 100; ++index) {
        values[index] = (long)index + 1;
    }
    CHECK(latency_percentile(values, 100, 50.0) == 50);
    CHECK(latency_percentile(values, 100, 95.0) == 95);
    CHECK(latency_percentile(values, 100, 99.0) == 99);
    CHECK(latency_percentile(values, 100, 100.0) == 100);
    CHECK(latency_percentile(values, 100, 0.0) == 1);
    CHECK(latency_percentile(values, 100, 50.5) == 51);
}

static void test_small_and_empty_samples(void)
{
    long one[1] = {7};
    long two[2] = {10, 20};

    CHECK(latency_percentile(one, 1, 50.0) == 7);
    CHECK(latency_percentile(one, 1, 99.0) == 7);
    CHECK(latency_percentile(two, 2, 50.0) == 10);
    CHECK(latency_percentile(two, 2, 51.0) == 20);
    CHECK(latency_percentile(one, 0, 50.0) == 0);
}

static void test_sort_orders_ascending(void)
{
    long values[5] = {30, 10, 50, 20, 40};
    size_t index;

    latency_sort(values, 5);
    for (index = 0; index < 5; ++index) {
        CHECK(values[index] == (long)(index + 1) * 10);
    }
}

static void test_mean(void)
{
    long values[4] = {10, 20, 30, 40};

    CHECK(latency_mean(values, 4) == 25.0);
    CHECK(latency_mean(values, 0) == 0.0);
}

int main(void)
{
    test_percentiles_use_nearest_rank();
    test_small_and_empty_samples();
    test_sort_orders_ascending();
    test_mean();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("latency stats tests passed\n");
    return 0;
}

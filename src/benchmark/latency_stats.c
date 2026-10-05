#include "benchmark/latency_stats.h"

#include <stdlib.h>

static int compare_longs(const void *left, const void *right)
{
    long a = *(const long *)left;
    long b = *(const long *)right;

    return (a > b) - (a < b);
}

void latency_sort(long *values, size_t count)
{
    qsort(values, count, sizeof(*values), compare_longs);
}

long latency_percentile(const long *sorted_values, size_t count, double percent)
{
    double exact_rank;
    size_t rank;

    if (count == 0) {
        return 0;
    }

    exact_rank = percent * (double)count / 100.0;
    rank = (size_t)exact_rank;
    if ((double)rank < exact_rank) {
        ++rank;
    }
    if (rank < 1) {
        rank = 1;
    }
    if (rank > count) {
        rank = count;
    }
    return sorted_values[rank - 1];
}

double latency_mean(const long *values, size_t count)
{
    double total = 0.0;
    size_t index;

    if (count == 0) {
        return 0.0;
    }
    for (index = 0; index < count; ++index) {
        total += (double)values[index];
    }
    return total / (double)count;
}

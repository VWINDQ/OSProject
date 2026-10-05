#ifndef CINEMA_LATENCY_STATS_H
#define CINEMA_LATENCY_STATS_H

#include <stddef.h>

void latency_sort(long *values, size_t count);

/* Nearest-rank percentile of a sample sorted ascending; 0 for an empty sample. */
long latency_percentile(const long *sorted_values, size_t count, double percent);

double latency_mean(const long *values, size_t count);

#endif

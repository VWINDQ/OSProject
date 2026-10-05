CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS ?= -lrt
THREADS = -pthread
INCLUDES = -Isrc

CLIENT_SOURCES = src/client/client.c
SERVER_SOURCES = src/server/server.c src/reservation/reservation.c src/utils/logger.c src/utils/server_lock.c
LOAD_TEST_SOURCES = src/benchmark/load_test.c src/benchmark/latency_stats.c
HEADERS = $(wildcard src/*/*.h)

.NOTPARALLEL:
.PHONY: all clean test test-unit test-smoke test-experiments test-load

all: client server load_test

client: $(CLIENT_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(CLIENT_SOURCES) -o $@ $(LDLIBS)

server: $(SERVER_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) $(SERVER_SOURCES) -o $@ $(LDLIBS)

load_test: $(LOAD_TEST_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) $(LOAD_TEST_SOURCES) -o $@ $(LDLIBS)

raw_request: tests/raw_request.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) tests/raw_request.c -o $@ $(LDLIBS)

test_logger: tests/test_logger.c src/utils/logger.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) tests/test_logger.c src/utils/logger.c -o $@ $(LDLIBS)

test_reservation: tests/test_reservation.c src/reservation/reservation.c src/utils/logger.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) tests/test_reservation.c src/reservation/reservation.c src/utils/logger.c -o $@ $(LDLIBS)

test_server_lock: tests/test_server_lock.c src/utils/server_lock.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_server_lock.c src/utils/server_lock.c -o $@ $(LDLIBS)

test_latency_stats: tests/test_latency_stats.c src/benchmark/latency_stats.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_latency_stats.c src/benchmark/latency_stats.c -o $@ $(LDLIBS)

test-unit: test_logger test_reservation test_server_lock test_latency_stats
	timeout 60 ./test_logger
	timeout 60 ./test_reservation
	timeout 60 ./test_server_lock
	timeout 60 ./test_latency_stats

test-smoke: all raw_request
	bash scripts/smoke_test.sh

test-experiments: all
	ROUNDS=3 RESULTS_DIR=/tmp/cinema_results bash scripts/experiments.sh all

test-load: all
	bash scripts/load_test.sh 2 1

test: test-unit test-smoke test-experiments test-load

clean:
	rm -f client server load_test raw_request test_logger test_reservation test_server_lock test_latency_stats

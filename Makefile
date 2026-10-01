CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS ?= -lrt
THREADS = -pthread

SERVER_SOURCES = server.c reservation.c logger.c
SERVER_HEADERS = common.h reservation.h logger.h

.NOTPARALLEL:
.PHONY: all clean test test-unit test-smoke test-experiments

all: client server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o $@ $(LDLIBS)

server: $(SERVER_SOURCES) $(SERVER_HEADERS)
	$(CC) $(CFLAGS) $(THREADS) $(SERVER_SOURCES) -o $@ $(LDLIBS)

raw_request: tests/raw_request.c common.h
	$(CC) $(CFLAGS) -I. tests/raw_request.c -o $@ $(LDLIBS)

test_logger: tests/test_logger.c logger.c logger.h
	$(CC) $(CFLAGS) -I. $(THREADS) tests/test_logger.c logger.c -o $@ $(LDLIBS)

test_reservation: tests/test_reservation.c reservation.c logger.c reservation.h logger.h common.h
	$(CC) $(CFLAGS) -I. $(THREADS) tests/test_reservation.c reservation.c logger.c -o $@ $(LDLIBS)

test-unit: test_logger test_reservation
	timeout 60 ./test_logger
	timeout 60 ./test_reservation

test-smoke: all raw_request
	bash scripts/smoke_test.sh

test-experiments: all
	ROUNDS=3 RESULTS_DIR=/tmp/cinema_results bash scripts/experiments.sh all

test: test-unit test-smoke test-experiments

clean:
	rm -f client server server_tsan raw_request test_logger test_reservation

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS ?= -lrt
THREADS = -pthread
INCLUDES = -Isrc

CLIENT_SOURCES = src/client/client.c
SERVER_SOURCES = src/server/server.c src/reservation/reservation.c src/utils/logger.c
HEADERS = $(wildcard src/*/*.h)

.NOTPARALLEL:
.PHONY: all clean test test-unit test-smoke test-experiments

all: client server

client: $(CLIENT_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(CLIENT_SOURCES) -o $@ $(LDLIBS)

server: $(SERVER_SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) $(SERVER_SOURCES) -o $@ $(LDLIBS)

raw_request: tests/raw_request.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) tests/raw_request.c -o $@ $(LDLIBS)

test_logger: tests/test_logger.c src/utils/logger.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) tests/test_logger.c src/utils/logger.c -o $@ $(LDLIBS)

test_reservation: tests/test_reservation.c src/reservation/reservation.c src/utils/logger.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) $(THREADS) tests/test_reservation.c src/reservation/reservation.c src/utils/logger.c -o $@ $(LDLIBS)

test-unit: test_logger test_reservation
	timeout 60 ./test_logger
	timeout 60 ./test_reservation

test-smoke: all raw_request
	bash scripts/smoke_test.sh

test-experiments: all
	ROUNDS=3 RESULTS_DIR=/tmp/cinema_results bash scripts/experiments.sh all

test: test-unit test-smoke test-experiments

clean:
	rm -f client server raw_request test_logger test_reservation

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS ?= -lrt
THREADS = -pthread

.NOTPARALLEL:
.PHONY: all clean test test-unit test-smoke

all: client mock_server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o $@ $(LDLIBS)

mock_server: mock_server.c common.h
	$(CC) $(CFLAGS) mock_server.c -o $@ $(LDLIBS)

test_logger: tests/test_logger.c logger.c logger.h
	$(CC) $(CFLAGS) -I. $(THREADS) tests/test_logger.c logger.c -o $@ $(LDLIBS)

test-unit: test_logger
	timeout 60 ./test_logger

test-smoke: all
	bash scripts/smoke_test.sh

test: test-unit test-smoke

clean:
	rm -f client server server_tsan raw_request mock_server test_logger test_reservation

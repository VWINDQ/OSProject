CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
LDLIBS ?= -lrt

.PHONY: all clean test

all: client mock_server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o $@ $(LDLIBS)

mock_server: mock_server.c common.h
	$(CC) $(CFLAGS) mock_server.c -o $@ $(LDLIBS)

test: all
	./scripts/smoke_test.sh

clean:
	rm -f client mock_server

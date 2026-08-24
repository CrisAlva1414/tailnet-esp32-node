# Tests host-side (sin ESP-IDF). Flags completos según docs/format/c-style.md:
# -std=c11 -Wall -Wextra -Wpedantic -Werror
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror
TSNODE = ../../components/tsnode
PROTO = $(TSNODE)/src/proto

TESTS = test_h2

all: $(TESTS)

test_h2: test_h2.c $(PROTO)/h2.c $(PROTO)/tsnode_map.c $(TSNODE)/include/tsnode_err.h $(PROTO)/h2.h $(PROTO)/tsnode_map.h
	$(CC) $(CFLAGS) -I$(TSNODE)/include -I$(PROTO) -o $@ test_h2.c \
		$(PROTO)/h2.c $(PROTO)/tsnode_map.c

check: all
	./test_h2

clean:
	rm -f $(TESTS)

.PHONY: all check clean

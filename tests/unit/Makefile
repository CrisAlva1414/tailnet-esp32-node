# Tests host-side (sin ESP-IDF). Flags completos según docs/format/c-style.md:
# -std=c11 -Wall -Wextra -Wpedantic -Werror
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror
TSNODE = ../../components/tsnode
PROTO = $(TSNODE)/src/proto
CRYPTO = $(TSNODE)/src/crypto
VECTORS = protocol_vectors

TESTS = test_h2 test_blake2s

all: $(TESTS)

test_h2: test_h2.c $(PROTO)/h2.c $(PROTO)/tsnode_map.c $(TSNODE)/include/tsnode_err.h $(PROTO)/h2.h $(PROTO)/tsnode_map.h
	$(CC) $(CFLAGS) -I$(TSNODE)/include -I$(PROTO) -o $@ test_h2.c \
		$(PROTO)/h2.c $(PROTO)/tsnode_map.c

test_blake2s: test_blake2s.c $(CRYPTO)/blake2s.c $(CRYPTO)/blake2s.h \
	$(TSNODE)/src/tsnode.c $(VECTORS)/blake2s_vectors.h \
	$(TSNODE)/include/tsnode_err.h
	$(CC) $(CFLAGS) -I$(TSNODE)/include -I$(CRYPTO) -I$(VECTORS) -o $@ $< \
		$(CRYPTO)/blake2s.c $(TSNODE)/src/tsnode.c

check: all
	./test_h2
	./test_blake2s

clean:
	rm -f $(TESTS)

.PHONY: all check clean

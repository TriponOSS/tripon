# Tripon toolchain (ralt). C99, zero deps.
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

SRC := $(wildcard ralt/src/*.c)
HDR := $(wildcard ralt/src/*.h)

build/ralt: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC)

.PHONY: test bench clean usage
usage: build/ralt
	@./build/ralt 2>&1 | head -3

test: build/ralt
	sh tests/run.sh

bench: build/ralt
	sh bench/build_bench.sh

clean:
	rm -rf build

# Tripon toolchain (ralt). C99, zero deps.
CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

SRC := $(wildcard ralt/src/*.c)
HDR := $(wildcard ralt/src/*.h)

build/ralt: $(SRC) $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC)

.PHONY: test bench clean
test: build/ralt
	sh tests/run.sh && sh tests/determinism.sh

bench: build/ralt
	sh bench/build_bench.sh

clean:
	rm -rf build

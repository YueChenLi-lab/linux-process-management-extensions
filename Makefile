CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS = -Iinclude
PYTHON ?= python3
BINS = build/pcb_queues build/scheduler build/nice_benchmark
.PHONY: all test demo benchmark clean
all: $(BINS)
build:
	mkdir -p build
build/%: src/%.c include/parse.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
test: all
	./build/pcb_queues --test
	./build/scheduler --test
	bash tests/cli_tests.sh
demo: all
	bash scripts/run_examples.sh
benchmark: all
	mkdir -p results/nice
	./build/nice_benchmark --rounds 10 --iterations 100000000 --seed 20261029 --output results/nice/raw.csv
	$(PYTHON) scripts/analyze_benchmark.py results/nice/raw.csv results/nice
clean:
	rm -rf build

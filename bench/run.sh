#!/bin/bash
# Builds and runs the benchmarks, with the same compiler flags each time.
cd "$(dirname "$0")/.."
CC=${CC:-cc}
CFLAGS="-std=c99 -O2 -Wall -Wno-unused-parameter"
SOURCES=$(ls src/*.c | grep -v main.c)
mkdir -p build

echo "== 20.3 hash table =="
$CC $CFLAGS $SOURCES bench/table_bench.c -o build/table_bench -lm || exit 1
build/table_bench

echo
echo "== 20.1 number keys with Lua's hash =="
$CC $CFLAGS -DLUA_DOUBLE_HASH $SOURCES bench/table_bench.c -o build/table_bench_lua -lm || exit 1
build/table_bench_lua numbers

echo
echo "== 20.3 churn with the book's rule for growing =="
$CC $CFLAGS -DBOOK_TABLE_GROWTH $SOURCES bench/table_bench.c -o build/table_bench_growth -lm || exit 1
build/table_bench_growth churn

echo
echo "== 21.2 indexed slots =="
$CC $CFLAGS $SOURCES bench/globals_bench.c -o build/globals_slots -lm || exit 1
build/globals_slots

echo
echo "== 21.1 hash table, constants reused =="
$CC $CFLAGS -DGLOBAL_TABLE $SOURCES bench/globals_bench.c -o build/globals_dedup -lm || exit 1
build/globals_dedup

echo
echo "== hash table, no constants reused =="
$CC $CFLAGS -DGLOBAL_TABLE -DNO_CONSTANT_DEDUP $SOURCES bench/globals_bench.c -o build/globals_nodedup -lm || exit 1
build/globals_nodedup

echo
echo "== 22.1 finding locals with the book's linear scan =="
$CC $CFLAGS $SOURCES bench/locals_bench.c -o build/locals_scan -lm || exit 1
build/locals_scan

echo
echo "== 22.1 finding locals with a hash table =="
$CC $CFLAGS -DLOCAL_HASH $SOURCES bench/locals_bench.c -o build/locals_hash -lm || exit 1
build/locals_hash

echo
echo "== 24.1 ip in a local variable, 24.3 checked natives (default) =="
$CC $CFLAGS $SOURCES bench/calls_bench.c -o build/calls_default -lm || exit 1
build/calls_default

echo
echo "== 24.1 the book's frame->ip (-DFRAME_IP) =="
$CC $CFLAGS -DFRAME_IP $SOURCES bench/calls_bench.c -o build/calls_frameip -lm || exit 1
build/calls_frameip

echo
echo "== 24.3 the book's unchecked natives (-DUNCHECKED_NATIVES) =="
$CC $CFLAGS -DUNCHECKED_NATIVES $SOURCES bench/calls_bench.c -o build/calls_unchecked -lm || exit 1
build/calls_unchecked

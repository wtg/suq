#!/bin/sh
# Builds and runs the queue logic tests; no window or raylib needed.
set -eu

cd "$(dirname "$0")"

CC=${CC:-cc}
CFLAGS=${CFLAGS:-"-std=c99 -O2 -Wall -Wextra -Wpedantic"}

mkdir -p build
# shellcheck disable=SC2086
$CC $CFLAGS -Isrc tests/test_queue.c src/queue.c -o build/test_queue
./build/test_queue

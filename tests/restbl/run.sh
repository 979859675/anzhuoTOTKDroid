#!/bin/sh
# Host tests of runtime/services/restbl.c (the game's resource size table): builds with the host compiler and runs them.
set -e
cd "$(dirname "$0")/../.."
out=${TMPDIR:-/tmp}/restbl_test
gcc -std=gnu11 -Wall -Wextra -Werror -O1 -g -Iruntime/services -Ithird_party/zstd -o "$out" \
    tests/restbl/test_restbl.c runtime/services/restbl.c third_party/zstd/zstddeclib.c
"$out"

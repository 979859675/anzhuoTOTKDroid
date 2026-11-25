#!/bin/sh
# Host tests of runtime/exefs_patch.c (the ExeFS patch readers): builds with the host compiler and runs them.
set -e
cd "$(dirname "$0")/../.."
out=${TMPDIR:-/tmp}/exefs_test
gcc -std=gnu11 -Wall -Wextra -Werror -O1 -g -Iruntime -o "$out" tests/exefs/test_exefs.c runtime/exefs_patch.c
"$out"

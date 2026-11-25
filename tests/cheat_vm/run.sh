#!/bin/sh
# Host tests of runtime/cheat_vm.c (the cheat VM and parser): builds with the host compiler and runs them.
set -e
cd "$(dirname "$0")/../.."
out=${TMPDIR:-/tmp}/cheat_vm_test
gcc -std=gnu11 -Wall -Wextra -Werror -O1 -g -Iruntime -o "$out" tests/cheat_vm/test_cheat_vm.c runtime/cheat_vm.c
"$out"

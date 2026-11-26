#!/bin/sh
# Host tests of runtime/settings_cheats_line.h (the cheats_on line of the settings file): builds with the host compiler and runs them.
set -e
cd "$(dirname "$0")/../.."
out=${TMPDIR:-/tmp}/settings_test
gcc -std=gnu11 -Wall -Wextra -Werror -O1 -g -Iruntime -o "$out" tests/settings/test_settings.c
"$out"

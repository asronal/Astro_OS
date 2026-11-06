#!/bin/sh
#
# Build and run the JPL Horizons tests on the build PC, using a libnova source
# tree whose src/*.c files have been compiled into libnova.a.
#
# Usage: run-host.sh <libnova source dir> <path to libnova.a>
#

set -eu

CORE_DIR="$(dirname "$0")/.."
TEST_BIN="$(mktemp)"
trap 'rm -f "${TEST_BIN}"' EXIT

g++ -std=c++17 -Wall -Wextra -O2 -I"${CORE_DIR}/include" -I"$1/src" \
	"${CORE_DIR}/src/sky.cpp" "${CORE_DIR}/tests/test_horizons.cpp" "$2" -lm -o "${TEST_BIN}"

"${TEST_BIN}" "${CORE_DIR}/tests/horizons.csv" "${CORE_DIR}/tests/horizons-rst.csv"

#!/usr/bin/env bash
# Builds and runs the modules/Bots standalone test suite.
#
# Deliberately independent of the core build: these tests cover the module's own
# logic (login state machine, naming rules, config and feature gates) and need
# nothing but a C++17 compiler. No Boost, no OpenSSL, no MySQL client headers,
# no cmake, no worldserver.
#
# Run from anywhere:   bash modules/Bots/tests/run_tests.sh
#
# Override the compiler with CXX=clang++ if you want a second opinion.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(cd "${HERE}/.." && pwd)"
CXX="${CXX:-g++}"
OUT_DIR="${BOTS_TEST_OUT:-${HERE}/.build}"

mkdir -p "${OUT_DIR}"

SOURCES=(
  "${MODULE_DIR}/src/BotIdentity.cpp"
  "${MODULE_DIR}/src/BotLifecyclePlan.cpp"
  "${MODULE_DIR}/src/BotConfig.cpp"
  "${HERE}/TestBotIdentity.cpp"
  "${HERE}/TestBotLifecyclePlan.cpp"
  "${HERE}/TestBotConfig.cpp"
  "${HERE}/main.cpp"
)

echo "compiling with ${CXX} ($(${CXX} -dumpversion))"
echo "output: ${OUT_DIR}/bots_tests"
echo

"${CXX}" \
  -std=c++17 \
  -Wall -Wextra \
  -Wshadow \
  -Wnon-virtual-dtor \
  -Wold-style-cast \
  -Wcast-align \
  -Wunused \
  -Woverloaded-virtual \
  -Wpedantic \
  -Wconversion \
  -Wsign-conversion \
  -Wnull-dereference \
  -Wdouble-promotion \
  -Wimplicit-fallthrough \
  -g -O1 \
  -I "${MODULE_DIR}/src" \
  -o "${OUT_DIR}/bots_tests" \
  "${SOURCES[@]}"

echo "build ok, running"
echo
"${OUT_DIR}/bots_tests"

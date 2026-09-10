#!/usr/bin/env bash
# ============================================================================
#  Daedalus :: scripts/verify.sh
#
#  The full gate, from a clean build directory: configure with warnings as
#  errors, build every target, run the unit suite, run CTest, start the server
#  and run the HTTP smoke test, then exercise the CLI and the benchmarks.
#
#  This is what to run before pushing, and what CI reproduces.
#
#  Usage:  ./scripts/verify.sh [build-dir]
# ============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-verify}"
FAILED=0

step() { printf '\n=== %s %s\n' "$1" "$(printf '%.0s=' $(seq 1 $((66 - ${#1}))))"; }

run() {
    local label="$1"
    shift
    if "$@"; then
        printf '  PASS  %s\n' "$label"
    else
        printf '  FAIL  %s\n' "$label"
        FAILED=$((FAILED + 1))
    fi
}

step "clean configure with -Werror"
rm -rf "$BUILD"
if ! cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DDAEDALUS_WERROR=ON; then
    echo "  FAIL  configure"
    exit 1
fi

step "build every target"
if ! cmake --build "$BUILD" -j "$(nproc)"; then
    echo "  FAIL  build"
    exit 1
fi
echo "  PASS  build (zero warnings, -Werror was on)"

step "unit tests"
run "daedalus_tests" "$BUILD/bin/daedalus_tests"

step "ctest, one entry per module"
run "ctest" ctest --test-dir "$BUILD" --output-on-failure

step "HTTP smoke test against a live server"
run "smoke.sh" bash "$ROOT/scripts/smoke.sh" "$BUILD/bin/daedalus_server" "$ROOT/web"

step "CLI"
run "daedalus_cli demo" bash -c "'$BUILD/bin/daedalus_cli' demo > /dev/null"
run "daedalus_cli version" bash -c "'$BUILD/bin/daedalus_cli' version > /dev/null"
run "unknown command exits 2" bash -c "'$BUILD/bin/daedalus_cli' nonsense > /dev/null 2>&1; [ \$? -eq 2 ]"

step "benchmarks"
run "daedalus_bench containers" bash -c "'$BUILD/bin/daedalus_bench' containers > /dev/null"

step "install to a temporary prefix"
PREFIX="$(mktemp -d)"
run "cmake --install" cmake --install "$BUILD" --prefix "$PREFIX"
run "headers were installed" test -f "$PREFIX/include/daedalus/core/Container.hpp"
run "package config was installed" test -f "$PREFIX/lib/cmake/Daedalus/DaedalusConfig.cmake"
rm -rf "$PREFIX"

printf '\n%s\n' "$(printf '%.0s=' $(seq 1 70))"
if [ "$FAILED" -eq 0 ]; then
    printf '  ALL CHECKS PASSED\n'
    printf '%s\n' "$(printf '%.0s=' $(seq 1 70))"
    exit 0
fi
printf '  %d CHECK(S) FAILED\n' "$FAILED"
printf '%s\n' "$(printf '%.0s=' $(seq 1 70))"
exit 1

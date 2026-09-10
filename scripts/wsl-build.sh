#!/usr/bin/env bash
# ============================================================================
#  Daedalus :: scripts/wsl-build.sh
#
#  Builds and tests from a Windows host through WSL2.
#
#  The project lives on the Windows filesystem, but compiling directly against
#  /mnt/c is extremely slow -- every header read crosses the 9p filesystem
#  bridge. This mirrors the sources onto the Linux filesystem first, which cuts
#  a full build from minutes to seconds, and leaves the Windows copy as the
#  single source of truth.
#
#  Usage:  ./scripts/wsl-build.sh [build|test|filter <name>|clean]
# ============================================================================
set -euo pipefail

SOURCE_DIR="${DAEDALUS_SOURCE:-/mnt/c/Users/ayush/Downloads/Daedalus}"
WORK_DIR="${DAEDALUS_WORK:-$HOME/daedalus-work}"
JOBS="${DAEDALUS_JOBS:-$(nproc)}"
BUILD_TYPE="${DAEDALUS_BUILD_TYPE:-Debug}"

command="${1:-test}"

# Results are mirrored back to the Windows side so the host can read them
# without another round trip through wsl.exe argument quoting.
LOG_DIR="$SOURCE_DIR/.wsl-logs"

sync_sources() {
    mkdir -p "$WORK_DIR" "$LOG_DIR"
    # Mirror what the build needs, preserving timestamps so make can still skip
    # unchanged translation units -- a plain cp would touch every file and force
    # a full rebuild every run.
    for item in include tests examples benchmarks CMakeLists.txt web; do
        if [ -e "$SOURCE_DIR/$item" ]; then
            cp -ru --preserve=timestamps "$SOURCE_DIR/$item" "$WORK_DIR/"
        fi
    done
    # Delete work-tree files that no longer exist in the source tree.
    for tracked in include tests examples benchmarks web; do
        [ -d "$WORK_DIR/$tracked" ] || continue
        (cd "$WORK_DIR/$tracked" && find . -type f) | while read -r relative; do
            if [ ! -e "$SOURCE_DIR/$tracked/${relative#./}" ]; then
                rm -f "$WORK_DIR/$tracked/${relative#./}"
            fi
        done
    done
}

configure() {
    cmake -S "$WORK_DIR" -B "$WORK_DIR/build" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DDAEDALUS_WERROR=ON > /dev/null
}

case "$command" in
    clean)
        rm -rf "$WORK_DIR"
        echo "removed $WORK_DIR"
        ;;
    build)
        sync_sources
        configure
        cmake --build "$WORK_DIR/build" -j"$JOBS" > "$LOG_DIR/build.log" 2>&1
        status=$?
        tail -30 "$LOG_DIR/build.log"
        exit $status
        ;;
    test)
        sync_sources
        configure
        if ! cmake --build "$WORK_DIR/build" -j"$JOBS" > "$LOG_DIR/build.log" 2>&1; then
            tail -40 "$LOG_DIR/build.log"
            exit 1
        fi
        # A hard timeout so a hang shows up as a failure rather than a stall.
        set +e
        stdbuf -oL -eL timeout 300 "$WORK_DIR/build/bin/daedalus_tests" --verbose \
            > "$LOG_DIR/test.log" 2>&1
        status=$?
        set -e
        [ $status -eq 124 ] && echo "TIMED OUT after 300s" >> "$LOG_DIR/test.log"
        echo "exit=$status" >> "$LOG_DIR/test.log"
        tail -25 "$LOG_DIR/test.log"
        exit $status
        ;;
    verify)
        sync_sources
        cp "$SOURCE_DIR/scripts/verify.sh" "$WORK_DIR/scripts-verify.sh" 2>/dev/null || true
        mkdir -p "$WORK_DIR/scripts"
        cp "$SOURCE_DIR/scripts/verify.sh" "$SOURCE_DIR/scripts/smoke.sh" "$WORK_DIR/scripts/"
        set +e
        stdbuf -oL -eL timeout 900 bash "$WORK_DIR/scripts/verify.sh"             "$WORK_DIR/build-verify" > "$LOG_DIR/verify.log" 2>&1
        status=$?
        set -e
        [ $status -eq 124 ] && echo "TIMED OUT after 900s" >> "$LOG_DIR/verify.log"
        echo "exit=$status" >> "$LOG_DIR/verify.log"
        tail -45 "$LOG_DIR/verify.log"
        exit $status
        ;;
    smoke)
        sync_sources
        configure
        cmake --build "$WORK_DIR/build" -j"$JOBS" > "$LOG_DIR/build.log" 2>&1
        set +e
        stdbuf -oL -eL timeout 180 bash "$SOURCE_DIR/scripts/smoke.sh" \
            "$WORK_DIR/build/bin/daedalus_server" "$SOURCE_DIR/web" \
            > "$LOG_DIR/smoke.log" 2>&1
        status=$?
        set -e
        [ $status -eq 124 ] && echo "TIMED OUT after 180s" >> "$LOG_DIR/smoke.log"
        echo "exit=$status" >> "$LOG_DIR/smoke.log"
        tail -60 "$LOG_DIR/smoke.log"
        ;;
    filter)
        sync_sources
        configure
        cmake --build "$WORK_DIR/build" -j"$JOBS" > "$LOG_DIR/build.log" 2>&1
        set +e
        timeout 120 "$WORK_DIR/build/bin/daedalus_tests" --filter="${2:-}" --verbose \
            > "$LOG_DIR/test.log" 2>&1
        status=$?
        set -e
        [ $status -eq 124 ] && echo "TIMED OUT after 120s" >> "$LOG_DIR/test.log"
        echo "exit=$status" >> "$LOG_DIR/test.log"
        tail -40 "$LOG_DIR/test.log"
        ;;
    *)
        echo "usage: $0 [build|test|filter <name>|clean]" >&2
        exit 2
        ;;
esac

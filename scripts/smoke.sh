#!/usr/bin/env bash
# ============================================================================
#  Daedalus :: scripts/smoke.sh
#
#  End-to-end check of the running server: starts it on a free port, exercises
#  the public and authenticated endpoints with curl, and asserts on what comes
#  back. This is the test that would have caught a broken route table, a
#  mis-scoped auth middleware or a missing web asset -- none of which the unit
#  tests can see.
#
#  Usage:  ./scripts/smoke.sh [path-to-daedalus_server] [web-root]
# ============================================================================
set -uo pipefail

SERVER_BIN="${1:-$HOME/daedalus-work/build/bin/daedalus_server}"
WEB_ROOT="${2:-$(cd "$(dirname "$0")/.." && pwd)/web}"
PORT="${DAEDALUS_PORT:-18080}"
BASE="http://127.0.0.1:$PORT"
COOKIES="$(mktemp)"
LOG="$(mktemp)"

PASSED=0
FAILED=0

check() {
    local label="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        PASSED=$((PASSED + 1))
        printf '  ok    %-52s %s\n' "$label" "$actual"
    else
        FAILED=$((FAILED + 1))
        printf '  FAIL  %-52s expected %s, got %s\n' "$label" "$expected" "$actual"
    fi
}

contains() {
    local label="$1" needle="$2" haystack="$3"
    if printf '%s' "$haystack" | grep -q -- "$needle"; then
        PASSED=$((PASSED + 1))
        printf '  ok    %-52s contains %s\n' "$label" "$needle"
    else
        FAILED=$((FAILED + 1))
        printf '  FAIL  %-52s missing %s\n' "$label" "$needle"
        printf '        got: %.200s\n' "$haystack"
    fi
}

status() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

cleanup() {
    [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
    wait "${SERVER_PID:-}" 2>/dev/null
    rm -f "$COOKIES" "$LOG"
}
trap cleanup EXIT

if [ ! -x "$SERVER_BIN" ]; then
    echo "server binary not found: $SERVER_BIN" >&2
    echo "build it first: ./scripts/wsl-build.sh build" >&2
    exit 2
fi

echo "Daedalus smoke test"
echo "  server:   $SERVER_BIN"
echo "  web root: $WEB_ROOT"
echo "  port:     $PORT"
echo

"$SERVER_BIN" --port "$PORT" --web-root "$WEB_ROOT" --seed --quiet > "$LOG" 2>&1 &
SERVER_PID=$!

# Wait for the port to answer rather than sleeping a fixed amount.
for _ in $(seq 1 50); do
    if curl -s -o /dev/null "$BASE/api/health" 2>/dev/null; then break; fi
    sleep 0.1
done

echo "-- public endpoints"
check "GET /api/health"                200 "$(status "$BASE/api/health")"
contains "health names the library"    daedalus "$(curl -s "$BASE/api/health")"
check "GET / serves the playground"    200 "$(status "$BASE/")"
contains "index.html is the real page" "Daedalus" "$(curl -s "$BASE/")"
check "GET /static/styles.css"         200 "$(status "$BASE/static/styles.css")"
check "GET /static/app.js"             200 "$(status "$BASE/static/app.js")"
check "GET /api/routes"                200 "$(status "$BASE/api/routes")"
# An unknown /api/ path answers 401, not 404, because the auth middleware runs
# before routing. That is deliberate: an anonymous client must not be able to
# enumerate which API endpoints exist.
check "unknown api path is 401"        401 "$(status "$BASE/api/nope")"
check "unknown non-api path is 404"    404 "$(status "$BASE/no-such-page")"

echo
echo "-- security"
contains "CSP header is sent"          "Content-Security-Policy" "$(curl -s -D - -o /dev/null "$BASE/api/health")"
contains "nosniff is sent"             "nosniff" "$(curl -s -D - -o /dev/null "$BASE/api/health")"
# --path-as-is stops curl collapsing the ".." itself, so the server really does
# receive the traversal attempt and gets to reject it.
check "raw traversal is rejected"      400 "$(status --path-as-is "$BASE/../../etc/passwd")"
# An encoded traversal decodes to "..", which normalisePath refuses outright --
# a 400 rather than a 404, because the request itself is malformed.
check "encoded traversal is rejected"  400 "$(status "$BASE/static/..%2f..%2fCMakeLists.txt")"
check "static file escape fails"       404 "$(status "$BASE/static/CMakeLists.txt")"

echo
echo "-- authentication is required"
check "sort needs a session"           401 "$(status -X POST "$BASE/api/sort" -d '{"values":[1]}')"
check "tree needs a session"           401 "$(status -X POST "$BASE/api/tree" -d '{"values":[1]}')"
check "admin needs a session"          401 "$(status "$BASE/api/admin/users")"
check "me needs a session"             401 "$(status "$BASE/api/auth/me")"

echo
echo "-- login"
check "wrong password is 401"          401 "$(status -X POST "$BASE/api/auth/login" \
                                             -d '{"username":"admin","password":"nope"}')"
LOGIN=$(curl -s -c "$COOKIES" -X POST "$BASE/api/auth/login" \
        -d '{"username":"admin","password":"Minotaur-Thread-2026"}')
contains "admin can sign in"           '"ok":true' "$LOGIN"
contains "role comes back"             '"role":"admin"' "$LOGIN"
contains "session cookie is HttpOnly"  "HttpOnly" "$(curl -s -D - -o /dev/null -X POST \
                                             "$BASE/api/auth/login" \
                                             -d '{"username":"admin","password":"Minotaur-Thread-2026"}')"

echo
echo "-- authenticated API"
check "me now works"                   200 "$(status -b "$COOKIES" "$BASE/api/auth/me")"
SORTED=$(curl -s -b "$COOKIES" -X POST "$BASE/api/sort" \
         -d '{"algorithm":"quick","values":[5,3,8,1,9,2]}')
contains "quicksort returns sorted"    '"sorted":\[1,2,3,5,8,9\]' "$SORTED"
contains "comparisons are counted"     '"comparisons"' "$SORTED"

TREE=$(curl -s -b "$COOKIES" -X POST "$BASE/api/tree" \
       -d '{"kind":"avl","values":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}')
contains "AVL stays shallow"           '"height":3' "$TREE"
contains "in-order is sorted"          '"inOrder":\[1,2,3' "$TREE"

BST=$(curl -s -b "$COOKIES" -X POST "$BASE/api/tree" \
      -d '{"kind":"bst","values":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}')
contains "plain BST degenerates"       '"height":14' "$BST"

GRAPH=$(curl -s -b "$COOKIES" -X POST "$BASE/api/graph" \
        -d '{"algorithm":"dijkstra","source":"A","edges":[{"from":"A","to":"B","weight":4},{"from":"B","to":"C","weight":3},{"from":"A","to":"C","weight":9}]}')
contains "dijkstra finds 7 not 9"      '"C":7' "$GRAPH"

MST=$(curl -s -b "$COOKIES" -X POST "$BASE/api/graph" \
      -d '{"algorithm":"mst-kruskal","edges":[{"from":"A","to":"B","weight":1},{"from":"B","to":"C","weight":2},{"from":"A","to":"C","weight":9}]}')
contains "kruskal picks the cheap tree" '"totalWeight":3' "$MST"

STRINGS=$(curl -s -b "$COOKIES" -X POST "$BASE/api/strings" \
          -d '{"algorithm":"kmp","text":"abababcabababc","pattern":"ababc"}')
contains "KMP finds both matches"      '"count":2' "$STRINGS"

check "admin list works for admin"     200 "$(status -b "$COOKIES" "$BASE/api/admin/users")"
contains "audit trail is populated"    '"action"' "$(curl -s -b "$COOKIES" "$BASE/api/admin/audit")"

echo
echo "-- role enforcement"
VIEWER_JAR="$(mktemp)"
curl -s -c "$VIEWER_JAR" -X POST "$BASE/api/auth/login" \
     -d '{"username":"viewer","password":"Minotaur-Thread-2026"}' > /dev/null
check "viewer can use the playground"  200 "$(status -b "$VIEWER_JAR" -X POST "$BASE/api/sort" \
                                             -d '{"values":[3,1,2]}')"
check "viewer is refused admin"        403 "$(status -b "$VIEWER_JAR" "$BASE/api/admin/users")"
rm -f "$VIEWER_JAR"

echo
echo "-- input validation"
check "bad algorithm is 400"           400 "$(status -b "$COOKIES" -X POST "$BASE/api/sort" \
                                             -d '{"algorithm":"nonsense","values":[1]}')"
check "non-numeric values are 400"     400 "$(status -b "$COOKIES" -X POST "$BASE/api/sort" \
                                             -d '{"values":["x"]}')"
check "oversized array is 400"         400 "$(status -b "$COOKIES" -X POST "$BASE/api/tree" \
                                             -d "{\"values\":[$(seq -s, 1 2500)]}")"
check "logout works"                   200 "$(status -b "$COOKIES" -c "$COOKIES" -X POST "$BASE/api/auth/logout")"
check "the session is dead afterwards" 401 "$(status -b "$COOKIES" "$BASE/api/auth/me")"

echo
echo "-----------------------------------------------------------"
printf '  %d passed, %d failed\n' "$PASSED" "$FAILED"
if [ "$FAILED" -eq 0 ]; then
    echo "  RESULT: PASS"
    echo "-----------------------------------------------------------"
    exit 0
fi
echo "  RESULT: FAIL"
echo "  server log:"
sed 's/^/    /' "$LOG"
echo "-----------------------------------------------------------"
exit 1

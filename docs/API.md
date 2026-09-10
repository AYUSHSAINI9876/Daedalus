# HTTP API reference

Base URL: `http://127.0.0.1:8080` by default.

Everything is JSON in and JSON out. Authentication is a session cookie
(`daedalus_session`), issued by `POST /api/auth/login` and marked
`HttpOnly; SameSite=Strict`.

## Authorisation model

One middleware sits in front of every `/api/` path:

| Path | Requirement |
| --- | --- |
| `/api/health`, `/api/routes`, `/api/auth/login`, `/api/auth/register` | public |
| everything else under `/api/` | a valid session |
| `/api/admin/**` | a valid session **with the admin role** |

Two consequences worth knowing:

- A newly added route is protected **by default**. Forgetting to secure an
  endpoint is not possible; forgetting to *un*-secure one is, and that fails
  safe.
- An unknown `/api/` path returns **401, not 404**. An anonymous client cannot
  enumerate which endpoints exist.

## Errors

```json
{ "error": "sign in to use this endpoint", "status": 401 }
```

| Status | Meaning |
| --- | --- |
| 400 | malformed JSON, bad path, invalid argument, oversized input |
| 401 | no session, or an expired one |
| 403 | authenticated but the role is insufficient |
| 404 | no such non-API path or static file |
| 405 | the path exists but not for this method (an `Allow` header is sent) |
| 429 | rate limited |
| 500 | unexpected — the detail is never sent to the client |

Every response carries `Content-Security-Policy`, `X-Content-Type-Options:
nosniff`, `X-Frame-Options: DENY` and `Referrer-Policy: no-referrer`.

---

## Public

### `GET /api/health`
```json
{ "status": "ok", "library": "daedalus", "version": "1.0.0",
  "users": 3, "sessions": 1 }
```

### `GET /api/routes`
Every registered route, as `"METHOD /pattern"`. Self-describing, so the UI does
not hard-code the map.

---

## Authentication

### `POST /api/auth/register`
```json
{ "username": "ayush", "email": "a@example.com",
  "password": "Labyrinth2026x", "role": "viewer" }
```
→ `201` on success, `400` with the reason otherwise.

Password policy: at least 12 characters, one uppercase, one lowercase, one
digit, and it must not contain the username.

Registration *does* reveal that a username is taken — it has to, or the user
cannot pick another. Login does not.

### `POST /api/auth/login`
```json
{ "username": "ayush", "password": "Labyrinth2026x" }
```
→ `200` and a `Set-Cookie`, or `401`, or `429` when rate limited.

The failure message is identical for an unknown user and a wrong password, and
comparable CPU is spent on both, so timing does not distinguish them either.

Five consecutive failures lock the account for 15 minutes. The counter is
committed before the failure is returned.

### `POST /api/auth/logout`
Revokes the session and clears the cookie. Always `200`.

### `GET /api/auth/me`
```json
{ "username": "ayush", "role": "admin" }
```

---

## The playground

### `GET /api/algorithms`
The names accepted by each endpoint below — sorting strategies, tree kinds,
graph algorithms, string algorithms.

### `POST /api/sort`
```json
{ "algorithm": "quick", "values": [5, 3, 8, 1, 9, 2] }
```
```json
{ "algorithm": "quick", "stable": false, "inPlace": true,
  "averageComplexity": "O(n log n)", "worstComplexity": "O(n^2)",
  "comparisons": 17, "swaps": 6, "milliseconds": 0.004,
  "sorted": [1, 2, 3, 5, 8, 9],
  "trace": ["comparison(0,0)", "swap(1,4)", "..."] }
```

`comparisons` and `swaps` are counted by an observer attached to the strategy
at runtime, not estimated. At most 2000 values.

### `POST /api/tree`
```json
{ "kind": "avl", "values": [1, 2, 3, 4, 5, 6, 7] }
```
Returns `size`, `height`, `balanced`, `minimum`, `maximum`, all four traversals
and a server-rendered ASCII `diagram`.

`kind` is one of `bst`, `avl`, `redblack`, `splay`, `treap`. Sending the same
ascending `values` to `bst` and then `avl` is the quickest way to see the
difference: heights of 14 and 3 for 15 keys.

### `POST /api/graph`
```json
{ "algorithm": "dijkstra", "directed": false, "source": "A",
  "edges": [ { "from": "A", "to": "B", "weight": 4 } ] }
```

| `algorithm` | Returns |
| --- | --- |
| `bfs`, `dfs` | `order` |
| `dijkstra`, `bellman-ford` | `distances` (null when unreachable), `negativeCycle`, `settled` |
| `mst-kruskal`, `mst-prim` | `edges`, `totalWeight`, `spansEveryVertex` |
| `topological` | `acyclic`, `order` |
| `scc` | `components` |
| `components` | `components`, `connected` |
| `bridges` | `bridges` |
| `articulation` | `articulationPoints` |

At most 5000 edges. `weight` defaults to 1. Dijkstra returns `400` on a negative
edge rather than a wrong answer.

### `POST /api/strings`
```json
{ "algorithm": "kmp", "text": "abababcabababc", "pattern": "ababc" }
```

| `algorithm` | Returns |
| --- | --- |
| `kmp`, `z`, `rabin-karp`, `boyer-moore` | `matches`, `count` |
| `palindrome` | `longestPalindrome` |
| `edit-distance` | `distance` |
| `lcs` | `subsequence`, `substring` |
| `suffix-array` | `suffixArray`, `longestRepeated` |

Text is capped at 100,000 characters.

---

## Admin

Both require the admin role.

### `GET /api/admin/users`
Every account: username, email, role, active flag, failed attempt count. Never
the password hash or the salt.

### `GET /api/admin/audit`
The audit ring buffer: action, username, client address, success flag, detail.
Bounded at 512 entries — oldest dropped.

---

## Static content

| Path | Serves |
| --- | --- |
| `GET /` | `web/index.html` |
| `GET /static/*path` | anything under the document root, LRU-cached |

Paths are percent-decoded and then normalised. Any `..` segment, backslash or
NUL is **refused with 400**, not sanitised. An encoded traversal
(`..%2f..%2f`) decodes to `..` and is caught by the same check.

---

## Worked example

```bash
./build/bin/daedalus_server --seed &

curl -s localhost:8080/api/health

curl -s -c jar -X POST localhost:8080/api/auth/login \
     -d '{"username":"admin","password":"Minotaur-Thread-2026"}'

curl -s -b jar -X POST localhost:8080/api/sort \
     -d '{"algorithm":"merge","values":[9,1,8,2,7,3]}'

# the same keys, two trees -- heights 14 and 3
for kind in bst avl; do
  curl -s -b jar -X POST localhost:8080/api/tree \
       -d "{\"kind\":\"$kind\",\"values\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}"
  echo
done
```

`scripts/smoke.sh` runs 40 assertions of exactly this shape.

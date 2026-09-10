# Daedalus

[![CI](https://github.com/AYUSHSAINI9876/Daedalus/actions/workflows/ci.yml/badge.svg)](https://github.com/AYUSHSAINI9876/Daedalus/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Dependencies](https://img.shields.io/badge/dependencies-none-brightgreen.svg)](#)

**A C++20 data structures and algorithms library, built around an
object-oriented core — with a hand-written HTTP server and an authenticated web
playground on top of it.**

Zero third-party dependencies. Nothing but a C++20 compiler and CMake. 374 unit
tests and 40 end-to-end HTTP assertions, all green, compiled with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror`.

---

## What this actually is

Three layers, each of which stands on the one below it:

| Layer | What it contains |
| --- | --- |
| **The library** | ~50 header-only structures and algorithms, behind a small polymorphic interface hierarchy |
| **The application** | A from-scratch HTTP/1.1 server, session authentication, RBAC, rate limiting and a JSON API — all built *on the library's own containers* |
| **The playground** | A dependency-free web UI that runs the algorithms live and visualises the result |

The middle layer is the point. A DSA library that only proves itself with unit
tests is a homework exercise; one whose router is the trie, whose session table
is the LRU cache, and whose rate limiter is the hash map has to actually work.

---

## Quick start

```bash
git clone https://github.com/AYUSHSAINI9876/Daedalus.git
cd Daedalus

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/bin/daedalus_tests                     # 374 tests
./build/bin/daedalus_cli demo                  # a tour of everything, in the terminal
./build/bin/daedalus_server --seed             # then open http://127.0.0.1:8080
./build/bin/daedalus_bench                     # measured against the standard library
```

On Windows, the host toolchain is unlikely to be new enough (MinGW GCC 6.3 has
no C++20). Build through WSL2 instead — there is a script for it:

```bash
./scripts/wsl-build.sh test     # mirrors the tree onto the Linux filesystem and builds there
./scripts/wsl-build.sh smoke    # starts the server and runs the HTTP assertions
```

Using it as a dependency:

```cmake
find_package(Daedalus REQUIRED)
target_link_libraries(your_target PRIVATE Daedalus::daedalus)
```

---

## The library

### Linear structures
`DynamicArray` (std::vector written out by hand — manual allocation, placement
new, rule of five, copy-and-swap), `SinglyLinkedList` (with in-place reversal,
Floyd cycle detection, nth-from-end), `DoublyLinkedList` (sentinel node, so
insert and erase have no null checks), `Deque` (growable ring buffer),
`CircularBuffer` (fixed capacity, overwrite or reject policy), `Stack` and
`Queue` (adapters over a pluggable store), `SkipList` (probabilistic, seeded).

### Trees
`BinarySearchTree`, `AVLTree`, `RedBlackTree`, `SplayTree`, `Treap`, `BTree`,
`BinaryHeap`/`PriorityQueue`, `SegmentTree` (with lazy propagation),
`FenwickTree` (plus the two-BIT range-update variant), `Trie`, and a general
non-ordered `BinaryTree` for the shape-based problems (invert, symmetry,
serialise, reconstruct from traversals, path sums, general LCA).

The five search trees share one `BinaryTreeBase<T, NodeT>`, so traversals
(iterative pre/in/post, level-order, **Morris** in O(1) space), validation,
rendering and the order queries are written exactly once — while AVL still
carries a height, red-black a colour, and treap a priority.

### Hashing
`HashMap` (separate chaining, prime table sizes), `OpenAddressingMap`
(**Robin Hood** probing with backward-shift deletion, not tombstones),
`HashSet` (with full set algebra), `BloomFilter` (sized from the standard
formulas, double hashing), `LRUCache` and `LFUCache` behind a shared `Cache`
interface.

### Graphs
Adjacency-list `Graph` with label→index mapping. BFS, DFS (iterative *and*
recursive), connected components, bipartite checking, cycle detection,
topological sort (Kahn and DFS), Dijkstra, Bellman-Ford, 0-1 BFS, A*,
Floyd-Warshall, Johnson, Kruskal, Prim, Tarjan and Kosaraju SCC, condensation,
bridges, articulation points, Edmonds-Karp, Dinic, min-cut, bipartite matching.

### Algorithms
12 sorting strategies behind one interface; 11 search variants; KMP, Z,
Rabin-Karp, Boyer-Moore-Horspool, Manacher, suffix array + Kasai LCP,
Aho-Corasick; the DP canon; backtracking (N-Queens, Sudoku, permutations,
subsets, maze, word search, graph colouring); greedy (activity selection,
fractional knapsack, Huffman, job sequencing); number theory (deterministic
Miller-Rabin, linear sieve, extended Euclid, CRT, fast doubling Fibonacci);
divide and conquer (inversions, closest pair, Karatsuba, quickselect,
median-of-medians).

---

## The object-oriented core

This is a DSA library *and* an OOP exercise, so the design patterns are real
and running, not described in a comment.

```
Container<T>                       size / empty / clear / name / toString
  ├── Collection<T>                insert / erase / contains / toVector
  │     ├── Sequence<T>            at / insertAt / eraseAt
  │     └── SortedSet<T>           minimum / maximum / height
  └── Map<K,V>                     put / get / erase / keys / entries
```

| Pattern | Where it lives | What it buys |
| --- | --- | --- |
| **Strategy** | `SortStrategy<T>`, `Comparator<T>` | pick the algorithm by name at runtime |
| **Observer** | `AlgorithmSubject` / `MetricsObserver` | count comparisons without touching the algorithm |
| **Visitor** | `TreeVisitor<T>` | traversal logic supplied by the caller |
| **Adapter** | `Stack`, `Queue` | one implementation, swappable backing store |
| **Factory** | `makeSortStrategy<T>` | build a strategy from a string |
| **Template Method** | `BinaryTreeBase` | shared traversals, per-tree balancing |
| **Decorator** | `ReverseComparator` | wrap an ordering without copying it |

`daedalus_cli patterns` runs all of them and prints what happened.

The exception hierarchy is single-rooted, so a caller can catch one precise
failure or blanket-catch the library:

```
DaedalusError → ContainerError → IndexOutOfRange / EmptyContainer / CapacityExceeded
              → LookupError    → KeyNotFound / DuplicateKey
              → GraphError     → VertexNotFound / NegativeCycleDetected
```

---

## The server and the playground

`daedalus_server` is a real HTTP/1.1 server written on raw sockets: an accept
loop, a bounded worker pool, keep-alive, receive timeouts, load shedding, and
graceful shutdown. It serves a JSON API and a small web UI.

**Every part of it is built on the library:**

| Server component | Structure it uses | Why that one |
| --- | --- | --- |
| Route matching | `Trie` (one node per path *segment*) | O(segments), independent of route count; gives static > parameter > wildcard precedence for free |
| Session store | `LRUCache` | the cache's eviction bound *is* the session cap, so a login flood cannot exhaust memory |
| Rate limiter | `HashMap` of token buckets | O(1) per request |
| Audit log | `CircularBuffer` | fixed size, oldest dropped |
| Static files | `LRUCache` | a busy page does not re-read the disk |
| Task queue | `Deque` behind a mutex | bounded, so backpressure blocks a producer instead of leaking |

### Authentication

Session auth with **PBKDF2-HMAC-SHA256** password hashing (per-user random
salt, 120,000 iterations), 256-bit session tokens from `std::random_device`
compared in constant time, HttpOnly + SameSite=Strict cookies, three-tier RBAC,
account lockout, per-client token-bucket rate limiting, and an audit trail.

SHA-256, HMAC and PBKDF2 are implemented from the specifications and verified
against the **official FIPS 180-4, RFC 4231 and RFC 7914 test vectors** in the
test suite.

> **Read this honestly.** These are correct implementations, not hardened ones.
> They make no attempt to resist cache-timing or power analysis, and PBKDF2 is
> a weaker password hash than Argon2id. For a real deployment, link libsodium.
> This exists so the demo has genuine password hashing rather than a comment
> claiming it does. See the header of `include/daedalus/auth/Crypto.hpp`.

Some deliberate choices worth pointing at:

- Login returns an identical failure for "no such user" and "wrong password",
  and burns comparable CPU on both, so the endpoint is not a username oracle.
- The lockout counter is committed **before** the failure is returned. State
  written on a failure path that gets rolled back protects nothing.
- Authorisation is one middleware in front of `/api/`, not a check repeated per
  handler — so a newly added route is protected by default. An unknown `/api/`
  path answers 401, not 404, so an anonymous client cannot enumerate the API.
- The page is served with a `script-src 'self'` CSP, so the UI has no inline
  scripts or styles at all.

### Trying it

```bash
./build/bin/daedalus_server --seed
# open http://127.0.0.1:8080
# sign in as admin / operator / viewer, password: Minotaur-Thread-2026
```

`--seed` is refused on a non-loopback address unless you also pass
`--allow-weak-seed`, because seeding fixed credentials on a public interface is
how a demo becomes an incident.

The playground lets you run every sorting algorithm on the same input and
compare the comparison counts, build the five search trees from the same keys
and watch the unbalanced one degenerate, run eleven graph algorithms on an edge
list you type, and see string matches highlighted in place.

---

## Testing

```bash
./build/bin/daedalus_tests              # everything
./build/bin/daedalus_tests --list       # every test name
./build/bin/daedalus_tests --filter=AVLTree --verbose
ctest --test-dir build --output-on-failure   # one CTest entry per module
./scripts/smoke.sh                      # 40 assertions against a live server
```

The test framework is bundled (`tests/framework/TestFramework.hpp`, ~300
lines) rather than pulled from a package manager, which keeps the promise that
a clean checkout builds and tests **offline** with nothing but a compiler.

What the suite actually checks, beyond "it returns the right answer":

- **Differential testing.** Every tree is run against `std::set` under a
  randomised insert/erase workload; the hash maps against `std::unordered_map`;
  every string matcher against a naive scan; Kruskal against Prim; Tarjan
  against Kosaraju; Dijkstra against Bellman-Ford against Floyd-Warshall.
- **Invariants after every mutation.** Red-black properties, AVL balance
  factors and stored heights, treap heap order, B-tree occupancy and uniform
  leaf depth.
- **Leak and double-free detection** via an element type that counts its own
  lifetime events.
- **Adversarial input.** Sorted input to a BST, all-duplicate input to
  quicksort, a chain union to disjoint-set, a collapsing hash to both maps, a
  200,000-vertex path to the iterative DFS.
- **Official crypto vectors**, not self-consistency.

### Bugs the tests actually caught

Worth listing, because a green suite that never failed is not evidence of
anything:

- **Miller-Rabin reported 73 as composite.** A witness that is a multiple of
  `n` reduces to 0, and every power of 0 stays 0. 73 divides the witness 28178.
  Fixed by skipping witnesses where `a % n == 0`.
- **`solveSudoku` hung forever** on a grid whose *givens* already conflicted.
  The solver only ever filled empty cells, so it never noticed, and searched
  the whole space to prove unsolvability. Now the givens are validated first.
- **`normalisePath("/a//b/./c")` returned `/a/b//c`.** Dropping a `.` segment
  has to drop one of its two surrounding slashes; rewritten as split-and-rejoin.
- **The test framework's own `CHECK_EQ` bound a reference to a temporary**, so
  `CHECK_EQ(x.value(), 3)` read freed memory.
- **`Deque<std::function>` would not compile**, because the value-based
  `Collection` interface forced `operator==` on every element type.

---

## Benchmarks

`./build/bin/daedalus_bench` measures against the standard library on the
machine running it. Representative numbers (WSL2, GCC 15.2, `-O3`, 4 cores):

| | vs standard library |
| --- | --- |
| `DynamicArray` append (2M) | **0.78×** `std::vector` |
| `RedBlackTree` insert (200k) | 1.13× `std::set`, **0.36×** on lookup |
| `Treap` insert (200k) | **0.72×** `std::set` |
| `OpenAddressingMap` insert (300k) | **0.87×** `std::unordered_map` |
| `HashMap` lookup (300k) | **0.59×** `std::unordered_map` |
| radix / counting sort (200k ints) | **0.37× / 0.29×** `std::sort` |
| comparison sorts (200k ints) | 5–17× *slower* than `std::sort` |

That last row is the interesting one, and the benchmark says so in its own
output: every comparison goes through a `std::function`, which is an indirect
call the optimiser cannot inline, while `std::sort` inlines its comparator
completely. **That is the measured price of choosing the algorithm by name at
runtime** — a constant factor, not a complexity difference. Reporting it is
more useful than hiding it.

---

## Layout

```
include/daedalus/
  core/         Container/Collection/Sequence/SortedSet/Map, exceptions,
                concepts, Comparator strategy, Observer instrumentation
  linear/       DynamicArray, linked lists, Deque, CircularBuffer,
                Stack, Queue, SkipList
  trees/        BST, AVL, red-black, splay, treap, B-tree, heaps,
                segment/Fenwick trees, trie, general binary tree
  hashing/      HashMap, OpenAddressingMap, HashSet, BloomFilter, caches
  graph/        Graph, traversal, shortest paths, MST, connectivity, flow
  sets/         DisjointSet
  algorithms/   sorting, searching, strings, DP, greedy, backtracking,
                number theory, divide and conquer
  concurrent/   BlockingQueue, ThreadPool
  auth/         SHA-256, HMAC, PBKDF2, AuthService, RBAC, rate limiting
  net/          Json, Http, Router, Server, Api
tests/          374 unit tests + the bundled framework
examples/       daedalus_cli, daedalus_server
benchmarks/     measured against the standard library
web/            the playground (no framework, no CDN, no build step)
scripts/        wsl-build.sh, smoke.sh
docs/           architecture, complexity tables, API reference
```

---

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — why the interfaces are shaped
  this way, and what each layer is allowed to depend on
- [docs/COMPLEXITY.md](docs/COMPLEXITY.md) — the full complexity table, and
  which entries the tests actually verify
- [docs/API.md](docs/API.md) — HTTP endpoint reference
- [CONTRIBUTING.md](CONTRIBUTING.md) — build, style and test conventions

## Licence

MIT — see [LICENSE](LICENSE).

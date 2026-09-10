# Complexity reference

Everything marked **[verified]** has a test that would fail if the bound were
violated — usually by asserting on a height, a probe distance, a comparison
count or a recursion depth, not by timing. The rest are the standard published
bounds.

`n` is the element count, `h` the height, `V`/`E` vertices and edges, `L` a key
or pattern length, `k` an alphabet or value range.

---

## Linear structures

| Structure | Access | Search | Insert | Erase | Space |
| --- | --- | --- | --- | --- | --- |
| `DynamicArray` | O(1) | O(n) | O(1) amortised at the back, O(n) elsewhere | O(n) | O(n) |
| `SinglyLinkedList` | O(n) | O(n) | O(1) at either end | O(1) front, O(n) back | O(n) |
| `DoublyLinkedList` | O(n), walks from the nearer end | O(n) | O(1) at either end | O(1) given a position | O(n) |
| `Deque` | O(1) | O(n) | O(1) amortised at both ends | O(1) at both ends | O(n) |
| `CircularBuffer` | O(1) | O(n) | O(1) | O(1) | O(capacity) |
| `Stack` / `Queue` | — | O(n) | O(1) amortised | O(1) amortised | O(n) |
| `SkipList` | — | O(log n) expected | O(log n) expected | O(log n) expected | O(n) expected |

- `DynamicArray` growth is geometric (×2 from 4). **[verified]** — a test
  asserts the capacity after 100 appends is between 100 and 256, so a linear
  growth policy would fail it.
- `Deque` at both ends is O(1) because the head index moves backwards through
  the ring modulo capacity; nothing is shifted. **[verified]** by a wrap-around
  test and a 20,000-operation churn test.
- `SkipList` height stays ≤ 24 for 4096 keys. **[verified]**

## Trees

| Structure | Search | Insert | Erase | Height | Space |
| --- | --- | --- | --- | --- | --- |
| `BinarySearchTree` | O(h) | O(h) | O(h) | O(n) worst | O(n) |
| `AVLTree` | O(log n) | O(log n) | O(log n) | ≤ 1.44 log₂(n+2) | O(n) |
| `RedBlackTree` | O(log n) | O(log n) | O(log n) | ≤ 2 log₂(n+1) | O(n) |
| `SplayTree` | O(log n) amortised | O(log n) amortised | O(log n) amortised | O(n) single op | O(n) |
| `Treap` | O(log n) expected | O(log n) expected | O(log n) expected | O(log n) expected | O(n) |
| `BTree(t)` | O(log_t n) node visits | O(log_t n) | O(log_t n) | O(log_t n) | O(n) |
| `BinaryHeap` | O(n) | O(log n) | O(log n) pop | O(log n) | O(n) |
| `SegmentTree` | O(log n) query | O(log n) point | — | — | O(n) |
| `LazySegmentTree` | O(log n) | O(log n) **range** | — | — | O(n) |
| `FenwickTree` | O(log n) | O(log n) | — | — | O(n), exactly n+1 |
| `Trie` | O(L) | O(L) | O(L) | — | O(total characters) |

- **[verified]** 1000 keys inserted in ascending order: BST height 999, AVL ≤ 14,
  red-black ≤ 20. This is the single clearest demonstration in the suite.
- **[verified]** AVL stored heights and balance factors are re-checked at every
  node after each mutation batch, not just the root.
- **[verified]** All five red-black properties plus parent-link consistency are
  re-checked after **every single erase** in a 300-key randomised teardown.
- **[verified]** B-tree occupancy `[t-1, 2t-1]` and uniform leaf depth after
  every erase, at `t = 2` where underflow happens constantly.
- **[verified]** `SegmentTree` builds in O(n) via Floyd's method rather than n
  separate O(log n) pushes; the heap test asserts a valid heap after bulk build.

## Hashing

| Structure | Average | Worst | Space |
| --- | --- | --- | --- |
| `HashMap` (chaining) | O(1) | O(n) | O(n) |
| `OpenAddressingMap` (Robin Hood) | O(1) | O(n) | O(capacity) |
| `HashSet` | O(1) | O(n) | O(n) |
| `BloomFilter` | O(k) always | O(k) always | m bits, independent of item size |
| `LRUCache` / `LFUCache` | O(1) | O(1) | O(capacity) |

- **[verified]** Robin Hood keeps the longest probe ≤ 32 across 3000 keys, and
  the benchmark measures 1 at a load factor near 0.7 with wrapping keys.
- **[verified]** Backward-shift deletion keeps lookups correct after deleting
  every other key from a run where all keys collide.
- **[verified]** Bloom filter: never a false negative across 1000 inserted keys;
  observed false-positive rate below 5% against a 1% target over 20,000 probes.
- **[verified]** Both caches are O(1) at eviction: LFU keeps frequency buckets
  with a running minimum rather than scanning.

## Union-find

| Operation | Complexity |
| --- | --- |
| `find` / `unite` | O(α(n)) amortised — below 5 for any realistic n |

**[verified]** A 100,000-element chain union followed by a full pass of finds
leaves a maximum depth of **1**. Either optimisation alone would give O(log n);
the test would fail with a depth in the tens.

## Sorting

| Algorithm | Best | Average | Worst | Space | Stable |
| --- | --- | --- | --- | --- | --- |
| bubble | O(n) | O(n²) | O(n²) | O(1) | yes |
| insertion | O(n) | O(n²) | O(n²) | O(1) | yes |
| selection | O(n²) | O(n²) | O(n²) | O(1) | no |
| shell | O(n log n) | ~O(n^1.3) | O(n^1.5) | O(1) | no |
| merge | O(n log n) | O(n log n) | O(n log n) | O(n) | yes |
| quick | O(n log n) | O(n log n) | O(n²) | O(log n) | no |
| heap | O(n log n) | O(n log n) | O(n log n) | O(1) | no |
| intro | O(n log n) | O(n log n) | **O(n log n)** | O(log n) | no |
| tim | O(n) | O(n log n) | O(n log n) | O(n) | yes |
| counting | O(n+k) | O(n+k) | O(n+k) | O(k) | yes |
| radix | O(d(n+k)) | same | same | O(n+k) | yes |
| bucket | O(n+k) | O(n+k) | O(n²) | O(n) | yes |

- **[verified]** Stability is checked by sorting keyed pairs and asserting the
  secondary field stays ordered — for every algorithm that claims it.
- **[verified]** Insertion sort uses < 2000 comparisons on 1000 sorted values,
  i.e. it really is O(n) in the best case.
- **[verified]** Quicksort on 20,000 **identical** values stays under 40n
  comparisons. A naive implementation is O(n²) here; three-way partitioning is
  what prevents it.
- **[verified]** Quicksort on 20,000 **sorted** values completes; median-of-three
  is what prevents the classic blow-up.
- Introsort's O(n log n) worst case comes from counting its own recursion depth
  and falling back to heapsort. This is what `std::sort` is.

## Searching

| Algorithm | Complexity | Requires |
| --- | --- | --- |
| linear | O(n) | nothing |
| binary | O(log n) | sorted |
| lower/upper bound | O(log n) | sorted |
| exponential | O(log i) | sorted; i is the answer's index |
| jump | O(√n) | sorted |
| interpolation | O(log log n) average, O(n) worst | sorted **and** uniformly distributed |
| ternary (unimodal max) | O(log n) | unimodal |
| rotated-array search | O(log n) | sorted then rotated |
| binary search on the answer | O(log range) | a monotone predicate |

Every midpoint is `low + (high - low) / 2`, never `(low + high) / 2` — the
latter overflows, and sat in the JDK for nine years.

## Strings

| Algorithm | Preprocess | Search | Space |
| --- | --- | --- | --- |
| naive | — | O(n·m) | O(1) |
| KMP | O(m) | O(n) | O(m) |
| Z algorithm | O(n+m) | O(n+m) | O(n+m) |
| Rabin-Karp | O(m) | O(n+m) average | O(1) |
| Boyer-Moore-Horspool | O(m+k) | O(n/m) best, O(n·m) worst | O(k) |
| Manacher | — | O(n) | O(n) |
| suffix array | O(n log² n) | — | O(n) |
| Kasai LCP | O(n) | — | O(n) |
| Aho-Corasick | O(total pattern length) | O(n + matches) | O(total) |
| edit distance | — | O(n·m) time, **O(min(n,m)) space** | rolling row |

**[verified]** Every matcher is checked against the naive scan on nine
hand-picked edge cases (overlaps, empty inputs, pattern longer than text) plus
60 random trials.

## Graphs

| Algorithm | Complexity | Note |
| --- | --- | --- |
| BFS / DFS | O(V+E) | DFS is iterative; **[verified]** on a 200,000-vertex path where recursion would overflow |
| connected components | O(V+E) | |
| bipartite check | O(V+E) | |
| cycle detection | O(V+E) | grey/black colouring for digraphs |
| topological sort | O((V+E) log V) | min-heap for a deterministic order |
| Dijkstra | O((V+E) log V) | lazy deletion; **throws** on a negative edge rather than returning a wrong answer |
| Bellman-Ford | O(V·E) | detects negative cycles |
| 0-1 BFS | O(V+E) | deque instead of a heap |
| A* | O((V+E) log V) | **[verified]** settles 441 of 1600 grid vertices where Dijkstra settles all 1600 |
| Floyd-Warshall | O(V³) | all pairs, dense |
| Johnson | O(V·E + V·E log V) | all pairs, sparse, negative edges allowed |
| Kruskal | O(E log E) | union-find is the cycle test |
| Prim | O(E log V) | lazy deletion |
| Tarjan SCC | O(V+E) | one pass |
| Kosaraju SCC | O(V+E) | two passes; **[verified]** agrees with Tarjan on 30 random digraphs |
| bridges / articulation points | O(V+E) | low-link |
| Edmonds-Karp | O(V·E²) | shortest augmenting path |
| Dinic | O(V²·E), O(E√V) unit capacity | **[verified]** agrees with Edmonds-Karp on 25 random networks |
| bipartite matching (Kuhn) | O(V·E) | |

## Dynamic programming

| Problem | Time | Space |
| --- | --- | --- |
| Kadane | O(n) | O(1) |
| 0/1 knapsack | O(n·W) | O(W) for the value, O(n·W) to reconstruct |
| unbounded knapsack | O(n·W) | O(W) |
| coin change (min / ways) | O(n·amount) | O(amount) |
| LIS | O(n log n) | O(n) |
| LCS | O(n·m) | O(min(n,m)) for the length |
| matrix chain | O(n³) | O(n²) |
| subset sum / partition | O(n·target) | O(target) |
| rod cutting | O(n²) | O(n) |
| house robber | O(n) | O(1) |
| grid path sum / count | O(r·c) | O(c) |

## Number theory

| Operation | Complexity |
| --- | --- |
| sieve of Eratosthenes | O(n log log n) |
| linear sieve | O(n), plus a smallest-prime-factor table |
| Miller-Rabin (deterministic < 2⁶⁴) | O(log³ n) |
| modular exponentiation | O(log e) |
| extended Euclid / modular inverse | O(log n) |
| CRT | O(k log n) |
| Fibonacci (fast doubling) | O(log n) |

**[verified]** Both sieves agree with each other and with Miller-Rabin on every
prime below 10,000, and π(10000) = 1229 is asserted exactly.

## Divide and conquer

| Algorithm | Complexity |
| --- | --- |
| inversion counting | O(n log n) |
| closest pair | O(n log n) |
| Karatsuba | O(n^1.585) |
| quickselect | O(n) expected |
| median of medians | **O(n) worst case** |
| Boyer-Moore majority | O(n) time, O(1) space |

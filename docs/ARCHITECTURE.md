# Architecture

## The dependency rule

Layers may only depend downwards. This is enforced by inspection rather than by
tooling, but it is the reason the library half can be lifted out and used on its
own.

```
   net/         Server, Router, Http, Json, Api        <- the application
   auth/        AuthService, Crypto                    <- the application
   concurrent/  ThreadPool, BlockingQueue
        |
        v
   algorithms/  sorting, searching, strings, DP, ...   <- consume structures
   graph/       Graph and its algorithms
        |
        v
   trees/  hashing/  linear/  sets/                    <- the structures
        |
        v
   core/        Container, Concepts, Exception,        <- the vocabulary
                Comparator, Observer, Version
```

`core/` depends on nothing but the standard library. Nothing in the library
half depends on `net/` or `auth/`.

---

## Why the interfaces look like this

```
Container<T>                       size / empty / clear / name / toString
  ├── Collection<T>                insert / erase / contains / toVector
  │     ├── Sequence<T>            at / insertAt / eraseAt
  │     └── SortedSet<T>           minimum / maximum / height
  └── Map<K,V>                     put / get / erase / keys / entries
```

The split is by **what you can ask**, not by how it is stored.

- `Sequence<T>` is the index-addressable interface. `DynamicArray`, both linked
  lists and `Deque` implement it, and a caller holding a `Sequence<int>&` cannot
  tell which.
- `SortedSet<T>` is the ordered interface, and it is what makes the five search
  trees interchangeable. `height()` is on this interface deliberately: the entire
  argument for a balanced tree is that this number stays logarithmic, so it has
  to be observable.
- `Map<K,V>` deliberately derives from `Container<K>` rather than
  `Collection<std::pair<K,V>>`. A map is not a bag of pairs — you cannot
  meaningfully `insert` a pair without deciding what happens to an existing key.

### The cost this design has

Virtual dispatch is not free, and the benchmarks say so out loud: the sorting
strategies are 5–17× slower than `std::sort` because every comparison goes
through a `std::function`. That is the honest price of runtime selection.

Where the cost was not acceptable, the design avoids it. The graph algorithms
are free function templates over a concrete `Graph<V, W>`, not virtual methods,
because the inner loop of Dijkstra runs millions of times. The polymorphic
`SortedSet` handle is for the CLI, the API and the tests — not for hot code.

---

## The template-parameter node type

The five search trees share `BinaryTreeBase<T, NodeT>`:

```cpp
template <typename T, typename NodeT>
class BinaryTreeBase : public SortedSet<T> { ... };
```

The node is a **template parameter**, not a fixed struct with every field union'd
together. That means:

- `AVLNode` carries an `int height`, `RBNode` a colour and a parent pointer,
  `TreapNode` a priority — and none of them pays for the others' fields.
- Traversals (iterative pre/in/post-order, level-order, Morris), BST validation,
  balance checking, the ASCII renderer, and the order queries (successor,
  predecessor, kth-smallest, LCA, range query) are written **once**.
- Only the balancing logic lives in the derived class, which is the only part
  that genuinely differs.

The base destructor frees every node, and can do so safely because destruction
depends on `NodeT` — a static type — and never on derived-class state.

The one method the base gets wrong for a derived class is `findNode`, which
`SplayTree` overrides because a lookup there restructures the tree.

---

## Instrumentation as a first-class concern

`AlgorithmSubject` and `AlgorithmObserver` exist so that a claim about
complexity can be turned into a number:

```cpp
auto strategy = makeSortStrategy<int>("bubble");
auto metrics  = std::make_shared<MetricsObserver>();
strategy->attach(metrics);
strategy->sort(data);
metrics->comparisons();   // 179294 on 600 shuffled values
```

Emission is guarded by `hasObservers()`, so an un-instrumented run costs one
predictable branch per event. The API exposes this directly: `/api/sort` returns
the comparison and swap counts alongside the sorted array, and the playground
draws the comparison table from them.

---

## The application layer

The server is not a bolt-on. Every part of it is built on the library, and that
is the point — a structure that has to survive a real request path is tested in
a way a unit test cannot reproduce.

| Component | Structure | Why that one |
| --- | --- | --- |
| Router | `Trie`, one node per path **segment** | O(segments) regardless of route count, and the static > parameter > wildcard precedence falls out of the node ordering rather than being coded |
| Session store | `LRUCache` | the eviction bound **is** the session cap; a login flood evicts rather than growing |
| Rate limiter | `HashMap<string, Bucket>` | O(1) per request |
| Audit log | `CircularBuffer` | fixed size, oldest dropped, no unbounded growth |
| Static files | `LRUCache` | a busy page does not re-read the disk |
| Connection queue | `Deque` behind a mutex and two condition variables | bounded, so backpressure blocks a producer instead of leaking |

### Request path

```
accept()
  └── ThreadPool::trySubmit           full queue -> 503, never block the acceptor
        └── read with a timeout       size-capped DURING the read
              └── parseRequest        strict; malformed -> 400
                    └── Router::dispatch
                          └── auth middleware      401 / 403 before routing
                                └── handler
                          └── withSecurityHeaders  CSP, nosniff, DENY
```

Authorisation is a middleware in front of `/api/`, not a check inside each
handler. A new route is therefore protected **by default**, and the failure mode
of forgetting something is "locked out", not "wide open".

---

## Deliberate trade-offs

**Thread-per-connection, not an event loop.** With a bounded pool and a bounded
queue this is the right shape for a demo server, and far easier to reason about
than epoll. It would not survive 10,000 idle connections; it is not trying to.

**In-memory only.** There is no persistence layer. Adding one would mean adding
a storage engine, which is a different project.

**Hand-written crypto.** The zero-dependency rule and the need for genuine
password hashing conflict. The resolution was to implement SHA-256, HMAC and
PBKDF2 from the specifications, verify them against the official test vectors,
and state plainly in the header and the README that they are correct but not
hardened, and that a real deployment should link libsodium. The alternative —
a comment claiming the passwords are hashed — would have been worse.

**A bundled test framework.** ~300 lines instead of a dependency, which is what
keeps "a clean checkout builds and tests offline" true. CI has a job that builds
in a network namespace with no connectivity to prove it.

# Contributing

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DDAEDALUS_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Option | Default | What it does |
| --- | --- | --- |
| `DAEDALUS_BUILD_TESTS` | ON | the unit suite |
| `DAEDALUS_BUILD_EXAMPLES` | ON | `daedalus_cli` and `daedalus_server` |
| `DAEDALUS_BUILD_BENCHMARKS` | ON | `daedalus_bench` |
| `DAEDALUS_WERROR` | OFF | warnings become errors — **CI turns this on** |
| `DAEDALUS_SANITIZE` | OFF | address + undefined-behaviour sanitizers |

### On Windows

The host toolchain is probably too old (MinGW GCC 6.3 has no C++20). Build
through WSL2:

```bash
./scripts/wsl-build.sh build     # sync + compile
./scripts/wsl-build.sh test      # + run the unit suite
./scripts/wsl-build.sh smoke     # + run the HTTP smoke test
./scripts/wsl-build.sh filter AVLTree
```

The script mirrors the tree onto the Linux filesystem first. Compiling directly
against `/mnt/c` is roughly twenty times slower, because every header read
crosses the 9p bridge — this was not a micro-optimisation, it was the difference
between a 10-second build and a 10-minute one.

Two traps worth knowing when driving WSL from Windows:

- `wsl.exe` mangles nested quotes in an inline `bash -c "..."`. Run a **script
  file** instead.
- Git Bash rewrites `/mnt/...` into a Windows path. Prefix with
  `MSYS_NO_PATHCONV=1`.

---

## Adding a structure

1. Put the header under the directory that matches its family.
2. Derive from the narrowest interface that fits — `Sequence<T>` if it is
   index-addressable, `SortedSet<T>` if it keeps order, `Collection<T>`
   otherwise.
3. Open the file with a comment block covering **what it is, why it exists
   rather than the obvious alternative, and its complexity**. The existing
   headers are the model; the "why" line is the part that matters.
4. Add a suite to the matching `tests/test_*.cpp` and register the suite name in
   `DAEDALUS_SUITES` in `CMakeLists.txt`, so CTest reports it separately.
5. If it is worth comparing against the standard library, add it to
   `benchmarks/benchmark_main.cpp`.

## Style

- 100 columns, 4-space indent, `.clang-format` is authoritative.
- `PascalCase` types, `camelCase` functions and variables, `trailing_` for
  private members, `kConstant` for constants.
- Full words: `values`, not `vals`. `predecessor`, not `pred`.
- `[[nodiscard]]` on anything returning a value with no side effect.
- Prefer `if constexpr` over SFINAE, and a concept over `enable_if`.
- Comments explain **why**, never what. If a line needs a comment saying what it
  does, rewrite the line.
- ASCII only in anything that reaches stdout. A Windows console in the cp1252
  code page throws on a non-ASCII write, which fails the run for the wrong
  reason. This has bitten a previous project; do not re-learn it.

---

## Testing

Use the bundled framework, not a dependency:

```cpp
DAEDALUS_TEST(SuiteName, what_it_should_do) {
    AVLTree<int> tree{3, 1, 2};
    CHECK_EQ(tree.toVector(), (std::vector<int>{1, 2, 3}));
    CHECK_TRUE(tree.isAVLBalanced());
    CHECK_THROWS_AS(tree.at(99), IndexOutOfRange);
}
```

Available: `CHECK_TRUE`, `CHECK_FALSE`, `CHECK_EQ`, `CHECK_NE`, `CHECK_LT`,
`CHECK_LE`, `CHECK_NEAR`, `CHECK_THROWS_AS`, `CHECK_NO_THROW`.

Note the extra parentheses around a braced initialiser — the preprocessor splits
on commas otherwise.

### What a good test looks like here

"It returns the right answer on one input" is the floor, not the bar. The suite
leans on four things, and a new structure should too:

- **A reference implementation.** Compare against `std::set`,
  `std::unordered_map`, `std::sort`, or a deliberately naive version, under a
  randomised workload with a fixed seed.
- **Invariants after every mutation**, not just at the end. The red-black tests
  re-verify all five properties after each individual erase; that is how the
  delete-fixup cases actually get covered.
- **The adversarial input.** Sorted input to a BST. All-duplicates to
  quicksort. A collapsing hash to a hash map. A 200,000-deep path to anything
  recursive.
- **Bounds, asserted.** If the claim is O(log n), assert on the height or the
  probe distance. Do not assert on wall-clock time — that is how a suite becomes
  flaky on a loaded CI runner.

Seed every RNG explicitly. A test that fails one run in fifty is worse than no
test.

---

## Before opening a pull request

```bash
cmake -S . -B build -DDAEDALUS_WERROR=ON && cmake --build build -j
ctest --test-dir build --output-on-failure
./scripts/smoke.sh
clang-format -i $(git ls-files '*.hpp' '*.cpp')
```

CI additionally runs the suite under address and UB sanitizers, and builds once
inside a network namespace with no connectivity — the zero-dependency claim is
tested, not asserted. If a change introduces a dependency, that job is what will
tell you.

// ============================================================================
//  Daedalus :: algorithms/Backtracking.hpp
//
//  Systematic search with pruning. Every routine here follows the same shape --
//  place a candidate, recurse, undo -- and the interesting part is always the
//  pruning: the constraint check that kills a whole subtree before it is
//  explored.
//
//    solveNQueens          column/diagonal occupancy, O(n!) worst but pruned
//                          hard enough that n = 12 is instant
//    solveSudoku           constraint propagation on rows, columns and boxes
//    permutations          all orderings, with a duplicate-safe variant
//    subsets               the power set, and subsets summing to a target
//    combinations          n choose k
//    solveMaze             rat-in-a-maze path finding
//    wordSearch            grid word search with visited marking
//    graphColouring        m-colouring feasibility
//
//  Solution counts are returned alongside the solutions, because for N-Queens
//  the count is the well-known sequence to check against (1, 0, 0, 2, 10, 4,
//  40, 92, ...).
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_BACKTRACKING_HPP
#define DAEDALUS_ALGORITHMS_BACKTRACKING_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

// --- N-Queens ----------------------------------------------------------------

/// One solution as the row index of the queen in each column.
using QueenPlacement = std::vector<std::size_t>;

struct NQueensResult {
    std::vector<QueenPlacement> solutions;
    std::size_t solutionCount{0};
    std::size_t nodesExplored{0};   ///< how much of the tree was actually walked
};

/// Places n non-attacking queens. Occupancy of columns and both diagonals is
/// tracked in flat arrays, so the "is this square safe?" test is O(1) rather
/// than a scan of the placed queens.
[[nodiscard]] inline NQueensResult solveNQueens(std::size_t n, bool countOnly = false) {
    NQueensResult result;
    if (n == 0) return result;

    std::vector<bool> rowUsed(n, false);
    std::vector<bool> risingUsed(2 * n - 1, false);    // row + column
    std::vector<bool> fallingUsed(2 * n - 1, false);   // row - column + n - 1
    QueenPlacement placement(n, 0);

    struct Solver {
        std::size_t n;
        bool countOnly;
        NQueensResult& result;
        std::vector<bool>& rowUsed;
        std::vector<bool>& risingUsed;
        std::vector<bool>& fallingUsed;
        QueenPlacement& placement;

        void place(std::size_t column) {
            ++result.nodesExplored;
            if (column == n) {
                ++result.solutionCount;
                if (!countOnly) result.solutions.push_back(placement);
                return;
            }
            for (std::size_t row = 0; row < n; ++row) {
                const std::size_t rising = row + column;
                const std::size_t falling = row + n - 1 - column;
                if (rowUsed[row] || risingUsed[rising] || fallingUsed[falling]) continue;

                rowUsed[row] = risingUsed[rising] = fallingUsed[falling] = true;
                placement[column] = row;
                place(column + 1);
                rowUsed[row] = risingUsed[rising] = fallingUsed[falling] = false;
            }
        }
    };

    Solver{n, countOnly, result, rowUsed, risingUsed, fallingUsed, placement}.place(0);
    return result;
}

/// Renders a placement as a board, for the CLI.
[[nodiscard]] inline std::string renderQueens(const QueenPlacement& placement) {
    const std::size_t n = placement.size();
    std::string board;
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t column = 0; column < n; ++column) {
            board += (placement[column] == row) ? 'Q' : '.';
        }
        board += '\n';
    }
    return board;
}

// --- Sudoku ------------------------------------------------------------------

using SudokuGrid = std::vector<std::vector<int>>;   ///< 9x9, zero means empty

/// Solves in place. Returns false when the puzzle has no solution. Picks the
/// most-constrained empty cell first, which cuts the search enormously versus
/// scanning in reading order.
[[nodiscard]] inline bool solveSudoku(SudokuGrid& grid) {
    require(grid.size() == 9, "sudoku grid must be 9x9");
    for (const auto& row : grid) require(row.size() == 9, "sudoku grid must be 9x9");

    const auto allowed = [&grid](std::size_t row, std::size_t column, int digit) {
        for (std::size_t i = 0; i < 9; ++i) {
            if (grid[row][i] == digit || grid[i][column] == digit) return false;
        }
        const std::size_t boxRow = (row / 3) * 3;
        const std::size_t boxColumn = (column / 3) * 3;
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                if (grid[boxRow + r][boxColumn + c] == digit) return false;
            }
        }
        return true;
    };

    // Reject a grid whose GIVENS already contradict each other before starting.
    // The solver only ever fills EMPTY cells, so it would never notice two
    // identical givens in one row -- it would instead search the entire space
    // to prove unsolvability, which on a sparse grid does not terminate in any
    // useful time. The contradiction is visible in O(1) per given, so check it.
    for (std::size_t row = 0; row < 9; ++row) {
        for (std::size_t column = 0; column < 9; ++column) {
            const int given = grid[row][column];
            if (given == 0) continue;
            if (given < 1 || given > 9) return false;
            grid[row][column] = 0;                       // hide it from its own check
            const bool consistent = allowed(row, column, given);
            grid[row][column] = given;
            if (!consistent) return false;
        }
    }

    using AllowedFn = decltype(allowed);

    struct Solver {
        SudokuGrid& grid;
        const AllowedFn& isAllowed;

        bool solve() {
            // Most-constrained cell: the empty square with the fewest options.
            std::size_t bestRow = 9;
            std::size_t bestColumn = 9;
            int fewest = 10;
            for (std::size_t row = 0; row < 9; ++row) {
                for (std::size_t column = 0; column < 9; ++column) {
                    if (grid[row][column] != 0) continue;
                    int options = 0;
                    for (int digit = 1; digit <= 9; ++digit) {
                        if (isAllowed(row, column, digit)) ++options;
                    }
                    if (options < fewest) {
                        fewest = options;
                        bestRow = row;
                        bestColumn = column;
                    }
                }
            }
            if (bestRow == 9) return true;   // no empty cells left
            if (fewest == 0) return false;   // a dead end, prune now

            for (int digit = 1; digit <= 9; ++digit) {
                if (!isAllowed(bestRow, bestColumn, digit)) continue;
                grid[bestRow][bestColumn] = digit;
                if (solve()) return true;
                grid[bestRow][bestColumn] = 0;
            }
            return false;
        }
    };

    return Solver{grid, allowed}.solve();
}

/// Checks a completed grid against the three Sudoku rules.
[[nodiscard]] inline bool isValidSudoku(const SudokuGrid& grid) {
    if (grid.size() != 9) return false;
    for (std::size_t unit = 0; unit < 9; ++unit) {
        std::vector<bool> rowSeen(10, false);
        std::vector<bool> columnSeen(10, false);
        std::vector<bool> boxSeen(10, false);
        for (std::size_t i = 0; i < 9; ++i) {
            const int rowDigit = grid[unit][i];
            const int columnDigit = grid[i][unit];
            const int boxDigit = grid[(unit / 3) * 3 + i / 3][(unit % 3) * 3 + i % 3];
            if (rowDigit < 1 || rowDigit > 9 || rowSeen[static_cast<std::size_t>(rowDigit)]) {
                return false;
            }
            if (columnDigit < 1 || columnDigit > 9 ||
                columnSeen[static_cast<std::size_t>(columnDigit)]) {
                return false;
            }
            if (boxDigit < 1 || boxDigit > 9 || boxSeen[static_cast<std::size_t>(boxDigit)]) {
                return false;
            }
            rowSeen[static_cast<std::size_t>(rowDigit)] = true;
            columnSeen[static_cast<std::size_t>(columnDigit)] = true;
            boxSeen[static_cast<std::size_t>(boxDigit)] = true;
        }
    }
    return true;
}

// --- permutations, subsets, combinations ------------------------------------

/// Every ordering of `values`. Assumes distinct elements; use
/// uniquePermutations when there may be repeats.
template <typename T>
[[nodiscard]] std::vector<std::vector<T>> permutations(std::vector<T> values) {
    std::vector<std::vector<T>> results;
    if (values.empty()) return results;

    struct Generator {
        std::vector<T>& values;
        std::vector<std::vector<T>>& results;

        void generate(std::size_t first) {
            if (first == values.size()) {
                results.push_back(values);
                return;
            }
            for (std::size_t i = first; i < values.size(); ++i) {
                std::swap(values[first], values[i]);
                generate(first + 1);
                std::swap(values[first], values[i]);   // undo: this is the backtrack
            }
        }
    };

    Generator{values, results}.generate(0);
    return results;
}

/// Distinct orderings when `values` may contain duplicates. Sorting first lets
/// the "skip an equal sibling" rule prune duplicate branches.
template <typename T>
[[nodiscard]] std::vector<std::vector<T>> uniquePermutations(std::vector<T> values) {
    std::sort(values.begin(), values.end());
    std::vector<std::vector<T>> results;
    if (values.empty()) return results;

    std::vector<bool> used(values.size(), false);
    std::vector<T> current;

    struct Generator {
        const std::vector<T>& values;
        std::vector<bool>& used;
        std::vector<T>& current;
        std::vector<std::vector<T>>& results;

        void generate() {
            if (current.size() == values.size()) {
                results.push_back(current);
                return;
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (used[i]) continue;
                // Only the first unused copy of a repeated value may start a
                // branch, or the same permutation is produced twice.
                if (i > 0 && values[i] == values[i - 1] && !used[i - 1]) continue;
                used[i] = true;
                current.push_back(values[i]);
                generate();
                current.pop_back();
                used[i] = false;
            }
        }
    };

    Generator{values, used, current, results}.generate();
    return results;
}

/// The power set, in a fixed order (each element either taken or not).
template <typename T>
[[nodiscard]] std::vector<std::vector<T>> subsets(const std::vector<T>& values) {
    std::vector<std::vector<T>> results;
    std::vector<T> current;

    struct Generator {
        const std::vector<T>& values;
        std::vector<T>& current;
        std::vector<std::vector<T>>& results;

        void generate(std::size_t index) {
            if (index == values.size()) {
                results.push_back(current);
                return;
            }
            generate(index + 1);              // skip values[index]
            current.push_back(values[index]);
            generate(index + 1);              // take it
            current.pop_back();
        }
    };

    Generator{values, current, results}.generate(0);
    return results;
}

/// All k-element combinations of 0..n-1, in lexicographic order.
[[nodiscard]] inline std::vector<std::vector<std::size_t>> combinations(std::size_t n,
                                                                        std::size_t k) {
    std::vector<std::vector<std::size_t>> results;
    if (k > n) return results;
    std::vector<std::size_t> current;

    struct Generator {
        std::size_t n;
        std::size_t k;
        std::vector<std::size_t>& current;
        std::vector<std::vector<std::size_t>>& results;

        void generate(std::size_t next) {
            if (current.size() == k) {
                results.push_back(current);
                return;
            }
            // Prune: stop when too few candidates remain to reach size k.
            for (std::size_t i = next; i + (k - current.size()) <= n; ++i) {
                current.push_back(i);
                generate(i + 1);
                current.pop_back();
            }
        }
    };

    Generator{n, k, current, results}.generate(0);
    return results;
}

/// Subsets of `values` summing exactly to `target`.
[[nodiscard]] inline std::vector<std::vector<long long>> subsetsSummingTo(
    const std::vector<long long>& values, long long target) {
    std::vector<std::vector<long long>> results;
    std::vector<long long> current;

    struct Generator {
        const std::vector<long long>& values;
        std::vector<long long>& current;
        std::vector<std::vector<long long>>& results;

        void generate(std::size_t index, long long remaining) {
            if (remaining == 0) {
                results.push_back(current);
                return;
            }
            if (index == values.size() || remaining < 0) return;
            current.push_back(values[index]);
            generate(index + 1, remaining - values[index]);
            current.pop_back();
            generate(index + 1, remaining);
        }
    };

    Generator{values, current, results}.generate(0, target);
    return results;
}

// --- grid problems -----------------------------------------------------------

/// Rat in a maze: a path of 'D'/'R'/'U'/'L' moves through a 0/1 grid where 1 is
/// open. Returns an empty string when no path exists.
[[nodiscard]] inline std::string solveMaze(const std::vector<std::vector<int>>& maze) {
    if (maze.empty() || maze[0].empty()) return "";
    const std::size_t rows = maze.size();
    const std::size_t columns = maze[0].size();
    if (maze[0][0] == 0 || maze[rows - 1][columns - 1] == 0) return "";

    std::vector<std::vector<bool>> visited(rows, std::vector<bool>(columns, false));
    std::string path;

    struct Walker {
        const std::vector<std::vector<int>>& maze;
        std::vector<std::vector<bool>>& visited;
        std::string& path;
        std::size_t rows;
        std::size_t columns;

        bool walk(std::size_t row, std::size_t column) {
            if (row >= rows || column >= columns) return false;
            if (maze[row][column] == 0 || visited[row][column]) return false;
            if (row == rows - 1 && column == columns - 1) return true;

            visited[row][column] = true;
            const char moves[] = {'D', 'R', 'U', 'L'};
            const std::size_t nextRow[] = {row + 1, row, row - 1, row};
            const std::size_t nextColumn[] = {column, column + 1, column, column - 1};
            for (std::size_t i = 0; i < 4; ++i) {
                // Underflow wraps to a huge value, which the bounds check
                // above rejects -- no separate negative test needed.
                path += moves[i];
                if (walk(nextRow[i], nextColumn[i])) return true;
                path.pop_back();
            }
            visited[row][column] = false;
            return false;
        }
    };

    if (!Walker{maze, visited, path, rows, columns}.walk(0, 0)) return "";
    return path;
}

/// Classic word search: can `word` be traced through adjacent grid cells
/// without reusing one?
[[nodiscard]] inline bool wordSearch(const std::vector<std::string>& grid,
                                     const std::string& word) {
    if (word.empty()) return true;
    if (grid.empty() || grid[0].empty()) return false;

    const std::size_t rows = grid.size();
    const std::size_t columns = grid[0].size();
    std::vector<std::vector<bool>> used(rows, std::vector<bool>(columns, false));

    struct Searcher {
        const std::vector<std::string>& grid;
        const std::string& word;
        std::vector<std::vector<bool>>& used;
        std::size_t rows;
        std::size_t columns;

        bool find(std::size_t row, std::size_t column, std::size_t index) {
            // Out of bounds, already on the path, or the wrong letter.
            if (row >= rows || column >= columns) return false;
            if (used[row][column] || grid[row][column] != word[index]) return false;
            if (index + 1 == word.size()) return true;   // that was the last letter

            used[row][column] = true;
            const bool found = find(row + 1, column, index + 1) ||
                               find(row, column + 1, index + 1) ||
                               find(row - 1, column, index + 1) ||
                               find(row, column - 1, index + 1);
            used[row][column] = false;
            return found;
        }
    };

    Searcher searcher{grid, word, used, rows, columns};
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            if (searcher.find(row, column, 0)) return true;
        }
    }
    return false;
}

// --- graph colouring ---------------------------------------------------------

/// Can the graph (given as an adjacency matrix) be coloured with `colours`
/// colours so no edge joins two equal colours? Returns the assignment when it
/// can. This is NP-complete in general; backtracking with the "try the smallest
/// unused colour first" ordering is the standard exact approach.
[[nodiscard]] inline std::vector<int> graphColouring(
    const std::vector<std::vector<bool>>& adjacency, int colours) {
    const std::size_t n = adjacency.size();
    std::vector<int> assignment(n, -1);
    if (n == 0 || colours <= 0) return {};

    struct Colourer {
        const std::vector<std::vector<bool>>& adjacency;
        std::vector<int>& assignment;
        int colours;
        std::size_t n;

        bool assign(std::size_t vertex) {
            if (vertex == n) return true;
            for (int colour = 0; colour < colours; ++colour) {
                bool conflict = false;
                for (std::size_t other = 0; other < n; ++other) {
                    if (adjacency[vertex][other] && assignment[other] == colour) {
                        conflict = true;
                        break;
                    }
                }
                if (conflict) continue;
                assignment[vertex] = colour;
                if (assign(vertex + 1)) return true;
                assignment[vertex] = -1;
            }
            return false;
        }
    };

    if (!Colourer{adjacency, assignment, colours, n}.assign(0)) return {};
    return assignment;
}

}  // namespace daedalus

#endif  // DAEDALUS_ALGORITHMS_BACKTRACKING_HPP

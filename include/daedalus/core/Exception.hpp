// ============================================================================
//  Daedalus :: core/Exception.hpp
//
//  A single-rooted exception hierarchy. Every error the library raises derives
//  from `DaedalusError`, so a caller can either catch one precise failure mode
//  or blanket-catch the whole library with one handler.
//
//      DaedalusError
//        |- ContainerError
//        |    |- IndexOutOfRange
//        |    |- EmptyContainer
//        |    |- CapacityExceeded
//        |- LookupError
//        |    |- KeyNotFound
//        |    |- DuplicateKey
//        |- InvalidArgument
//        |- GraphError
//             |- VertexNotFound
//             |- EdgeNotFound
//             |- NegativeCycleDetected
// ============================================================================
#ifndef DAEDALUS_CORE_EXCEPTION_HPP
#define DAEDALUS_CORE_EXCEPTION_HPP

#include <cstddef>
#include <stdexcept>
#include <string>

namespace daedalus {

/// Root of the library's exception hierarchy.
class DaedalusError : public std::runtime_error {
public:
    explicit DaedalusError(const std::string& what) : std::runtime_error(what) {}
};

// --- container faults --------------------------------------------------------

class ContainerError : public DaedalusError {
public:
    explicit ContainerError(const std::string& what) : DaedalusError(what) {}
};

/// Thrown by every checked element accessor (`at`, `removeAt`, ...).
class IndexOutOfRange : public ContainerError {
public:
    IndexOutOfRange(std::size_t index, std::size_t size)
        : ContainerError("index " + std::to_string(index) + " out of range for container of size " +
                         std::to_string(size)),
          index_(index),
          size_(size) {}

    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::size_t index_;
    std::size_t size_;
};

/// Thrown when an element is requested from an empty structure.
class EmptyContainer : public ContainerError {
public:
    explicit EmptyContainer(const std::string& op)
        : ContainerError("cannot perform '" + op + "' on an empty container") {}
};

/// Thrown by fixed-capacity structures (e.g. CircularBuffer in strict mode).
class CapacityExceeded : public ContainerError {
public:
    explicit CapacityExceeded(std::size_t capacity)
        : ContainerError("capacity of " + std::to_string(capacity) + " exceeded") {}
};

// --- lookup faults -----------------------------------------------------------

class LookupError : public DaedalusError {
public:
    explicit LookupError(const std::string& what) : DaedalusError(what) {}
};

class KeyNotFound : public LookupError {
public:
    explicit KeyNotFound(const std::string& key = {})
        : LookupError(key.empty() ? "key not found" : "key not found: " + key) {}
};

class DuplicateKey : public LookupError {
public:
    explicit DuplicateKey(const std::string& key = {})
        : LookupError(key.empty() ? "duplicate key" : "duplicate key: " + key) {}
};

// --- argument faults ---------------------------------------------------------

class InvalidArgument : public DaedalusError {
public:
    explicit InvalidArgument(const std::string& what) : DaedalusError(what) {}
};

// --- graph faults ------------------------------------------------------------

class GraphError : public DaedalusError {
public:
    explicit GraphError(const std::string& what) : DaedalusError(what) {}
};

class VertexNotFound : public GraphError {
public:
    explicit VertexNotFound(const std::string& v)
        : GraphError("vertex not present in graph: " + v) {}
};

class EdgeNotFound : public GraphError {
public:
    EdgeNotFound(const std::string& u, const std::string& v)
        : GraphError("edge not present in graph: " + u + " -> " + v) {}
};

/// Raised by algorithms whose result is undefined on a negative cycle
/// (Bellman-Ford in strict mode, Johnson's reweighting).
class NegativeCycleDetected : public GraphError {
public:
    NegativeCycleDetected() : GraphError("graph contains a negative-weight cycle") {}
};

/// Precondition helper: throws `InvalidArgument` when `cond` is false.
inline void require(bool cond, const std::string& message) {
    if (!cond) throw InvalidArgument(message);
}

}   // namespace daedalus

#endif   // DAEDALUS_CORE_EXCEPTION_HPP

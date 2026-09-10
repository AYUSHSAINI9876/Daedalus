// ============================================================================
//  Daedalus :: core/Observer.hpp
//
//  Algorithm instrumentation (Observer pattern). Sorters and graph algorithms
//  derive from AlgorithmSubject and emit events; observers attached at runtime
//  turn those into comparison/swap counts or a human-readable trace.
//
//  Emission is guarded by hasObservers(), so an un-instrumented run costs one
//  predictable branch per event and nothing else.
// ============================================================================
#ifndef DAEDALUS_CORE_OBSERVER_HPP
#define DAEDALUS_CORE_OBSERVER_HPP

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace daedalus {

enum class EventType {
    Comparison,  ///< two elements were compared
    Swap,        ///< two elements exchanged positions
    Move,        ///< an element was written to a new slot
    Access,      ///< an element was read
    Visit,       ///< a vertex or node was visited
    Relax,       ///< a shortest-path edge relaxation succeeded
    Partition,   ///< a divide-and-conquer split happened
    Custom       ///< algorithm-specific milestone, see `detail`
};

[[nodiscard]] inline std::string toString(EventType type) {
    switch (type) {
        case EventType::Comparison: return "comparison";
        case EventType::Swap:       return "swap";
        case EventType::Move:       return "move";
        case EventType::Access:     return "access";
        case EventType::Visit:      return "visit";
        case EventType::Relax:      return "relax";
        case EventType::Partition:  return "partition";
        case EventType::Custom:     return "custom";
    }
    return "unknown";
}

/// One instrumented step. `lhs`/`rhs` carry indices or vertex ids when the
/// event type implies a pair; `detail` carries free-form context.
struct AlgorithmEvent {
    EventType type{EventType::Custom};
    std::size_t lhs{0};
    std::size_t rhs{0};
    std::string detail;

    AlgorithmEvent() = default;
    AlgorithmEvent(EventType t, std::size_t l, std::size_t r, std::string d = {})
        : type(t), lhs(l), rhs(r), detail(std::move(d)) {}
    explicit AlgorithmEvent(EventType t, std::string d = {})
        : type(t), detail(std::move(d)) {}
};

/// Abstract listener.
class AlgorithmObserver {
public:
    virtual ~AlgorithmObserver() = default;
    virtual void onEvent(const AlgorithmEvent& event) = 0;
    virtual void reset() = 0;
    [[nodiscard]] virtual std::string report() const = 0;
};

/// Tallies events by type. This is what turns "quicksort is faster" into a
/// number you can put in a table.
class MetricsObserver final : public AlgorithmObserver {
public:
    void onEvent(const AlgorithmEvent& event) override { ++counts_[event.type]; }

    void reset() override { counts_.clear(); }

    [[nodiscard]] std::size_t count(EventType type) const {
        const auto it = counts_.find(type);
        return it == counts_.end() ? 0 : it->second;
    }

    [[nodiscard]] std::size_t comparisons() const { return count(EventType::Comparison); }
    [[nodiscard]] std::size_t swaps() const { return count(EventType::Swap); }
    [[nodiscard]] std::size_t total() const {
        std::size_t sum = 0;
        for (const auto& entry : counts_) sum += entry.second;
        return sum;
    }

    [[nodiscard]] std::string report() const override {
        std::ostringstream os;
        os << "{";
        bool first = true;
        for (const auto& entry : counts_) {
            if (!first) os << ", ";
            os << daedalus::toString(entry.first) << "=" << entry.second;
            first = false;
        }
        os << "}";
        return os.str();
    }

private:
    std::map<EventType, std::size_t> counts_;
};

/// Keeps the first `limit` events as text. Useful for the CLI trace mode and
/// for asserting on algorithm order in tests.
class TraceObserver final : public AlgorithmObserver {
public:
    explicit TraceObserver(std::size_t limit = 64) : limit_(limit) {}

    void onEvent(const AlgorithmEvent& event) override {
        ++seen_;
        if (lines_.size() >= limit_) return;
        std::ostringstream os;
        os << daedalus::toString(event.type);
        if (event.type != EventType::Custom) os << "(" << event.lhs << "," << event.rhs << ")";
        if (!event.detail.empty()) os << " " << event.detail;
        lines_.push_back(os.str());
    }

    void reset() override {
        lines_.clear();
        seen_ = 0;
    }

    [[nodiscard]] const std::vector<std::string>& lines() const noexcept { return lines_; }
    [[nodiscard]] std::size_t seen() const noexcept { return seen_; }

    [[nodiscard]] std::string report() const override {
        std::ostringstream os;
        for (std::size_t i = 0; i < lines_.size(); ++i) os << "  " << (i + 1) << ". " << lines_[i] << "\n";
        if (seen_ > lines_.size()) os << "  ... " << (seen_ - lines_.size()) << " more\n";
        return os.str();
    }

private:
    std::size_t limit_;
    std::size_t seen_{0};
    std::vector<std::string> lines_;
};

/// Mixin for anything that wants to be observed.
class AlgorithmSubject {
public:
    virtual ~AlgorithmSubject() = default;

    void attach(std::shared_ptr<AlgorithmObserver> observer) {
        if (observer) observers_.push_back(std::move(observer));
    }

    void detach(const std::shared_ptr<AlgorithmObserver>& observer) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                         observers_.end());
    }

    void detachAll() noexcept { observers_.clear(); }

    [[nodiscard]] bool hasObservers() const noexcept { return !observers_.empty(); }

protected:
    void emit(const AlgorithmEvent& event) const {
        for (const auto& observer : observers_) observer->onEvent(event);
    }

    void emit(EventType type, std::size_t lhs, std::size_t rhs) const {
        if (observers_.empty()) return;
        emit(AlgorithmEvent{type, lhs, rhs});
    }

private:
    std::vector<std::shared_ptr<AlgorithmObserver>> observers_;
};

}  // namespace daedalus

#endif  // DAEDALUS_CORE_OBSERVER_HPP

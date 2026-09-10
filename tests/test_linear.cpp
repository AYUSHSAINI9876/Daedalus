// ============================================================================
//  Unit tests for daedalus/linear/*
// ============================================================================
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include "daedalus/linear/CircularBuffer.hpp"
#include "daedalus/linear/Deque.hpp"
#include "daedalus/linear/DoublyLinkedList.hpp"
#include "daedalus/linear/DynamicArray.hpp"
#include "daedalus/linear/Queue.hpp"
#include "daedalus/linear/SinglyLinkedList.hpp"
#include "daedalus/linear/SkipList.hpp"
#include "daedalus/linear/Stack.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

/// Element type that counts its own lifetime events, so the containers can be
/// checked for leaks and for double destruction.
struct Tracked {
    static inline int liveCount = 0;
    static inline int copyCount = 0;
    static inline int moveCount = 0;

    int value{0};

    Tracked() { ++liveCount; }
    explicit Tracked(int v) : value(v) { ++liveCount; }
    Tracked(const Tracked& other) : value(other.value) {
        ++liveCount;
        ++copyCount;
    }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        ++liveCount;
        ++moveCount;
    }
    Tracked& operator=(const Tracked& other) {
        value = other.value;
        ++copyCount;
        return *this;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value;
        ++moveCount;
        return *this;
    }
    ~Tracked() { --liveCount; }

    bool operator==(const Tracked& other) const { return value == other.value; }

    static void resetCounters() {
        liveCount = 0;
        copyCount = 0;
        moveCount = 0;
    }
};

std::vector<int> iota(int count, int start = 0) {
    std::vector<int> values(static_cast<std::size_t>(count));
    std::iota(values.begin(), values.end(), start);
    return values;
}

}  // namespace

// ============================================================================
//  DynamicArray
// ============================================================================

DAEDALUS_TEST(DynamicArray, starts_empty) {
    DynamicArray<int> array;
    CHECK_TRUE(array.empty());
    CHECK_EQ(array.size(), 0u);
    CHECK_EQ(array.capacity(), 0u);
    CHECK_EQ(array.name(), std::string("DynamicArray"));
}

DAEDALUS_TEST(DynamicArray, push_back_grows_geometrically) {
    DynamicArray<int> array;
    for (int i = 0; i < 100; ++i) array.pushBack(i);
    CHECK_EQ(array.size(), 100u);
    CHECK_LE(100u, array.capacity());
    // Geometric growth from 4 gives 128, not 100 reallocations worth of slack.
    CHECK_LE(array.capacity(), 256u);
    for (int i = 0; i < 100; ++i) CHECK_EQ(array[static_cast<std::size_t>(i)], i);
}

DAEDALUS_TEST(DynamicArray, initializer_list_and_iteration) {
    DynamicArray<int> array{5, 3, 9, 1};
    CHECK_EQ(array.toVector(), (std::vector<int>{5, 3, 9, 1}));
    int sum = 0;
    for (int value : array) sum += value;
    CHECK_EQ(sum, 18);
}

DAEDALUS_TEST(DynamicArray, at_is_bounds_checked) {
    DynamicArray<int> array{1, 2, 3};
    CHECK_EQ(array.at(2), 3);
    CHECK_THROWS_AS(array.at(3), IndexOutOfRange);
    const DynamicArray<int>& constRef = array;
    CHECK_THROWS_AS(constRef.at(99), ContainerError);
}

DAEDALUS_TEST(DynamicArray, front_back_and_pop) {
    DynamicArray<int> array{10, 20, 30};
    CHECK_EQ(array.front(), 10);
    CHECK_EQ(array.back(), 30);
    CHECK_EQ(array.popBack(), 30);
    CHECK_EQ(array.size(), 2u);
    array.clear();
    CHECK_THROWS_AS(array.front(), EmptyContainer);
    CHECK_THROWS_AS(array.popBack(), EmptyContainer);
}

DAEDALUS_TEST(DynamicArray, insert_at_every_position) {
    DynamicArray<int> array{1, 2, 3};
    array.insertAt(0, 0);
    CHECK_EQ(array.toVector(), (std::vector<int>{0, 1, 2, 3}));
    array.insertAt(4, 4);
    CHECK_EQ(array.toVector(), (std::vector<int>{0, 1, 2, 3, 4}));
    array.insertAt(2, 99);
    CHECK_EQ(array.toVector(), (std::vector<int>{0, 1, 99, 2, 3, 4}));
    CHECK_THROWS_AS(array.insertAt(99, 7), IndexOutOfRange);
}

DAEDALUS_TEST(DynamicArray, erase_by_index_and_value) {
    DynamicArray<int> array{1, 2, 3, 2};
    CHECK_EQ(array.eraseAt(0), 1);
    CHECK_EQ(array.toVector(), (std::vector<int>{2, 3, 2}));
    CHECK_TRUE(array.erase(2));                       // removes the first 2 only
    CHECK_EQ(array.toVector(), (std::vector<int>{3, 2}));
    CHECK_FALSE(array.erase(42));
    CHECK_THROWS_AS(array.eraseAt(5), IndexOutOfRange);
}

DAEDALUS_TEST(DynamicArray, index_of_and_contains) {
    DynamicArray<int> array{4, 5, 6};
    CHECK_EQ(array.indexOf(5), 1u);
    CHECK_EQ(array.indexOf(42), DynamicArray<int>::kNotFound);
    CHECK_TRUE(array.contains(6));
    CHECK_FALSE(array.contains(7));
}

DAEDALUS_TEST(DynamicArray, resize_reserve_and_shrink) {
    DynamicArray<int> array;
    array.reserve(50);
    CHECK_LE(50u, array.capacity());
    array.resize(3, 7);
    CHECK_EQ(array.toVector(), (std::vector<int>{7, 7, 7}));
    array.resize(1);
    CHECK_EQ(array.toVector(), (std::vector<int>{7}));
    array.shrinkToFit();
    CHECK_EQ(array.capacity(), 1u);
}

DAEDALUS_TEST(DynamicArray, reverse_in_place) {
    DynamicArray<int> even{1, 2, 3, 4};
    even.reverse();
    CHECK_EQ(even.toVector(), (std::vector<int>{4, 3, 2, 1}));
    DynamicArray<int> odd{1, 2, 3};
    odd.reverse();
    CHECK_EQ(odd.toVector(), (std::vector<int>{3, 2, 1}));
    DynamicArray<int> empty;
    CHECK_NO_THROW(empty.reverse());
}

DAEDALUS_TEST(DynamicArray, copy_and_move_semantics) {
    DynamicArray<int> original{1, 2, 3};
    DynamicArray<int> copy(original);
    copy.pushBack(4);
    CHECK_EQ(original.size(), 3u);   // deep copy, not shared storage
    CHECK_EQ(copy.size(), 4u);

    DynamicArray<int> moved(std::move(copy));
    CHECK_EQ(moved.size(), 4u);

    DynamicArray<int> assigned;
    assigned = original;
    CHECK_TRUE(assigned == original);
}

DAEDALUS_TEST(DynamicArray, destroys_every_element_exactly_once) {
    Tracked::resetCounters();
    {
        DynamicArray<Tracked> array;
        for (int i = 0; i < 40; ++i) array.pushBack(Tracked(i));
        CHECK_EQ(Tracked::liveCount, 40);
        DynamicArray<Tracked> copy = array;   // exercises the copy path too
        CHECK_EQ(Tracked::liveCount, 80);
        array.eraseAt(0);
        CHECK_EQ(Tracked::liveCount, 79);
    }
    CHECK_EQ(Tracked::liveCount, 0);
}

DAEDALUS_TEST(DynamicArray, emplace_back_constructs_in_place) {
    DynamicArray<std::string> array;
    array.emplaceBack(3, 'x');
    CHECK_EQ(array.back(), std::string("xxx"));
}

DAEDALUS_TEST(DynamicArray, to_string_lists_elements) {
    DynamicArray<int> array{1, 2, 3};
    CHECK_EQ(array.toString(), std::string("DynamicArray [1, 2, 3]"));
}

// ============================================================================
//  SinglyLinkedList
// ============================================================================

DAEDALUS_TEST(SinglyLinkedList, push_and_pop_both_ends) {
    SinglyLinkedList<int> list;
    list.pushBack(2);
    list.pushBack(3);
    list.pushFront(1);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 3}));
    CHECK_EQ(list.popFront(), 1);
    CHECK_EQ(list.popBack(), 3);
    CHECK_EQ(list.size(), 1u);
    CHECK_EQ(list.front(), 2);
    CHECK_EQ(list.back(), 2);
}

DAEDALUS_TEST(SinglyLinkedList, tail_stays_correct_after_emptying) {
    SinglyLinkedList<int> list{1};
    CHECK_EQ(list.popBack(), 1);
    CHECK_TRUE(list.empty());
    list.pushBack(9);   // would corrupt if tail_ had been left dangling
    CHECK_EQ(list.toVector(), (std::vector<int>{9}));
}

DAEDALUS_TEST(SinglyLinkedList, insert_and_erase_at_index) {
    SinglyLinkedList<int> list{1, 2, 4};
    list.insertAt(2, 3);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 3, 4}));
    list.insertAt(4, 5);
    CHECK_EQ(list.back(), 5);
    CHECK_EQ(list.eraseAt(0), 1);
    CHECK_EQ(list.eraseAt(3), 5);          // erasing the tail must move tail_
    CHECK_EQ(list.toVector(), (std::vector<int>{2, 3, 4}));
    list.pushBack(6);
    CHECK_EQ(list.back(), 6);
    CHECK_THROWS_AS(list.eraseAt(99), IndexOutOfRange);
}

DAEDALUS_TEST(SinglyLinkedList, erase_by_value_updates_tail) {
    SinglyLinkedList<int> list{1, 2, 3};
    CHECK_TRUE(list.erase(3));
    list.pushBack(4);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 4}));
    CHECK_FALSE(list.erase(99));
}

DAEDALUS_TEST(SinglyLinkedList, reverse_updates_head_and_tail) {
    SinglyLinkedList<int> list{1, 2, 3, 4};
    list.reverse();
    CHECK_EQ(list.toVector(), (std::vector<int>{4, 3, 2, 1}));
    CHECK_EQ(list.front(), 4);
    CHECK_EQ(list.back(), 1);
    list.pushBack(0);
    CHECK_EQ(list.toVector(), (std::vector<int>{4, 3, 2, 1, 0}));
}

DAEDALUS_TEST(SinglyLinkedList, middle_node) {
    CHECK_FALSE(SinglyLinkedList<int>{}.middle().has_value());
    CHECK_EQ(SinglyLinkedList<int>({1}).middle().value(), 1);
    CHECK_EQ(SinglyLinkedList<int>({1, 2, 3}).middle().value(), 2);
    CHECK_EQ(SinglyLinkedList<int>({1, 2, 3, 4}).middle().value(), 3);
}

DAEDALUS_TEST(SinglyLinkedList, nth_from_end) {
    SinglyLinkedList<int> list{1, 2, 3, 4, 5};
    CHECK_EQ(list.nthFromEnd(1).value(), 5);
    CHECK_EQ(list.nthFromEnd(5).value(), 1);
    CHECK_FALSE(list.nthFromEnd(6).has_value());
    CHECK_FALSE(list.nthFromEnd(0).has_value());
}

DAEDALUS_TEST(SinglyLinkedList, floyd_cycle_detection) {
    SinglyLinkedList<int> list{1, 2, 3, 4, 5};
    CHECK_FALSE(list.hasCycle());
    list.makeCycleForTesting(2);
    CHECK_TRUE(list.hasCycle());
    list.breakCycleForTesting();
    CHECK_FALSE(list.hasCycle());
}

DAEDALUS_TEST(SinglyLinkedList, remove_consecutive_duplicates) {
    SinglyLinkedList<int> list{1, 1, 2, 3, 3, 3, 4};
    CHECK_EQ(list.removeConsecutiveDuplicates(), 3u);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 3, 4}));
    CHECK_EQ(list.size(), 4u);
    list.pushBack(5);
    CHECK_EQ(list.back(), 5);
}

DAEDALUS_TEST(SinglyLinkedList, copy_is_deep) {
    SinglyLinkedList<int> original{1, 2, 3};
    SinglyLinkedList<int> copy = original;
    copy.pushBack(4);
    CHECK_EQ(original.size(), 3u);
    CHECK_EQ(copy.size(), 4u);
}

DAEDALUS_TEST(SinglyLinkedList, releases_all_nodes) {
    Tracked::resetCounters();
    {
        SinglyLinkedList<Tracked> list;
        for (int i = 0; i < 20; ++i) list.pushBack(Tracked(i));
        CHECK_EQ(Tracked::liveCount, 20);
    }
    CHECK_EQ(Tracked::liveCount, 0);
}

// ============================================================================
//  DoublyLinkedList
// ============================================================================

DAEDALUS_TEST(DoublyLinkedList, push_and_pop_both_ends) {
    DoublyLinkedList<int> list;
    list.pushBack(2);
    list.pushFront(1);
    list.pushBack(3);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 3}));
    CHECK_EQ(list.popFront(), 1);
    CHECK_EQ(list.popBack(), 3);
    CHECK_EQ(list.size(), 1u);
    CHECK_THROWS_AS(DoublyLinkedList<int>{}.popBack(), EmptyContainer);
}

DAEDALUS_TEST(DoublyLinkedList, bidirectional_iteration) {
    DoublyLinkedList<int> list{1, 2, 3};
    std::vector<int> forward(list.begin(), list.end());
    CHECK_EQ(forward, (std::vector<int>{1, 2, 3}));

    auto it = list.end();
    std::vector<int> backward;
    while (it != list.begin()) {
        --it;
        backward.push_back(*it);
    }
    CHECK_EQ(backward, (std::vector<int>{3, 2, 1}));
}

DAEDALUS_TEST(DoublyLinkedList, erase_by_iterator_is_constant_time) {
    DoublyLinkedList<int> list{1, 2, 3, 4};
    auto it = list.begin();
    ++it;                       // -> 2
    auto next = list.erase(it);
    CHECK_EQ(*next, 3);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 3, 4}));
    CHECK_THROWS_AS(list.erase(list.end()), InvalidArgument);
}

DAEDALUS_TEST(DoublyLinkedList, node_at_walks_from_nearest_end) {
    DoublyLinkedList<int> list;
    for (int i = 0; i < 10; ++i) list.pushBack(i);
    CHECK_EQ(list.at(0), 0);
    CHECK_EQ(list.at(9), 9);
    CHECK_EQ(list.at(5), 5);
    CHECK_THROWS_AS(list.at(10), IndexOutOfRange);
}

DAEDALUS_TEST(DoublyLinkedList, insert_and_erase_at_index) {
    DoublyLinkedList<int> list{1, 3};
    list.insertAt(1, 2);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 2, 3}));
    list.insertAt(3, 4);
    CHECK_EQ(list.back(), 4);
    CHECK_EQ(list.eraseAt(0), 1);
    CHECK_EQ(list.toVector(), (std::vector<int>{2, 3, 4}));
}

DAEDALUS_TEST(DoublyLinkedList, reverse_keeps_links_consistent) {
    DoublyLinkedList<int> list{1, 2, 3, 4};
    list.reverse();
    CHECK_EQ(list.toVector(), (std::vector<int>{4, 3, 2, 1}));
    CHECK_EQ(list.front(), 4);
    CHECK_EQ(list.back(), 1);
    list.pushFront(5);
    list.pushBack(0);
    CHECK_EQ(list.toVector(), (std::vector<int>{5, 4, 3, 2, 1, 0}));
}

DAEDALUS_TEST(DoublyLinkedList, copy_move_and_swap) {
    DoublyLinkedList<int> a{1, 2, 3};
    DoublyLinkedList<int> b{9};
    a.swap(b);
    CHECK_EQ(a.toVector(), (std::vector<int>{9}));
    CHECK_EQ(b.toVector(), (std::vector<int>{1, 2, 3}));

    DoublyLinkedList<int> copy = b;
    copy.pushBack(4);
    CHECK_EQ(b.size(), 3u);
    CHECK_EQ(copy.size(), 4u);

    DoublyLinkedList<int> moved = std::move(copy);
    CHECK_EQ(moved.size(), 4u);
    moved.pushBack(5);              // moved-from source must still be usable
    CHECK_EQ(moved.size(), 5u);
}

DAEDALUS_TEST(DoublyLinkedList, releases_all_nodes) {
    Tracked::resetCounters();
    {
        DoublyLinkedList<Tracked> list;
        for (int i = 0; i < 25; ++i) list.pushFront(Tracked(i));
        CHECK_EQ(Tracked::liveCount, 25);
        list.clear();
        CHECK_EQ(Tracked::liveCount, 0);
    }
    CHECK_EQ(Tracked::liveCount, 0);
}

// ============================================================================
//  Deque
// ============================================================================

DAEDALUS_TEST(Deque, wraps_around_the_ring) {
    Deque<int> deque;
    for (int i = 0; i < 5; ++i) deque.pushBack(i);
    for (int i = 0; i < 3; ++i) (void)deque.popFront();
    for (int i = 100; i < 108; ++i) deque.pushBack(i);
    CHECK_EQ(deque.toVector(),
             (std::vector<int>{3, 4, 100, 101, 102, 103, 104, 105, 106, 107}));
}

DAEDALUS_TEST(Deque, push_front_walks_head_backwards) {
    Deque<int> deque;
    for (int i = 0; i < 20; ++i) deque.pushFront(i);
    std::vector<int> expected = iota(20);
    std::reverse(expected.begin(), expected.end());
    CHECK_EQ(deque.toVector(), expected);
}

DAEDALUS_TEST(Deque, random_access_and_bounds) {
    Deque<int> deque{1, 2, 3};
    CHECK_EQ(deque[0], 1);
    CHECK_EQ(deque.at(2), 3);
    CHECK_THROWS_AS(deque.at(3), IndexOutOfRange);
    CHECK_EQ(deque.front(), 1);
    CHECK_EQ(deque.back(), 3);
}

DAEDALUS_TEST(Deque, insert_and_erase_in_the_middle) {
    Deque<int> deque{1, 2, 4};
    deque.insertAt(2, 3);
    CHECK_EQ(deque.toVector(), (std::vector<int>{1, 2, 3, 4}));
    CHECK_EQ(deque.eraseAt(1), 2);
    CHECK_EQ(deque.toVector(), (std::vector<int>{1, 3, 4}));
    CHECK_TRUE(deque.erase(4));
    CHECK_FALSE(deque.erase(42));
}

DAEDALUS_TEST(Deque, insert_at_middle_survives_reallocation) {
    Deque<int> deque;
    for (int i = 0; i < 8; ++i) deque.pushBack(i);   // exactly at capacity
    deque.insertAt(4, 99);                           // forces a grow mid-insert
    CHECK_EQ(deque.toVector(), (std::vector<int>{0, 1, 2, 3, 99, 4, 5, 6, 7}));
}

DAEDALUS_TEST(Deque, empty_operations_throw) {
    Deque<int> deque;
    CHECK_THROWS_AS(deque.popFront(), EmptyContainer);
    CHECK_THROWS_AS(deque.popBack(), EmptyContainer);
    CHECK_THROWS_AS(deque.front(), EmptyContainer);
}

DAEDALUS_TEST(Deque, releases_all_elements) {
    Tracked::resetCounters();
    {
        Deque<Tracked> deque;
        for (int i = 0; i < 50; ++i) deque.pushFront(Tracked(i));
        CHECK_EQ(Tracked::liveCount, 50);
    }
    CHECK_EQ(Tracked::liveCount, 0);
}

// ============================================================================
//  Stack / Queue adapters
// ============================================================================

DAEDALUS_TEST(Stack, lifo_order) {
    Stack<int> stack;
    stack.push(1);
    stack.push(2);
    stack.push(3);
    CHECK_EQ(stack.peek(), 3);
    CHECK_EQ(stack.pop(), 3);
    CHECK_EQ(stack.pop(), 2);
    CHECK_EQ(stack.size(), 1u);
    CHECK_EQ(stack.pop(), 1);
    CHECK_THROWS_AS(stack.pop(), EmptyContainer);
    CHECK_THROWS_AS(stack.peek(), EmptyContainer);
}

DAEDALUS_TEST(Stack, runs_on_a_linked_store) {
    Stack<int, DoublyLinkedList<int>> stack;
    stack.push(1);
    stack.push(2);
    CHECK_EQ(stack.pop(), 2);
    CHECK_EQ(stack.toVector(), (std::vector<int>{1}));
}

DAEDALUS_TEST(Queue, fifo_order) {
    Queue<int> queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    CHECK_EQ(queue.peek(), 1);
    CHECK_EQ(queue.back(), 3);
    CHECK_EQ(queue.dequeue(), 1);
    CHECK_EQ(queue.dequeue(), 2);
    CHECK_EQ(queue.size(), 1u);
    CHECK_EQ(queue.dequeue(), 3);
    CHECK_THROWS_AS(queue.dequeue(), EmptyContainer);
}

DAEDALUS_TEST(Queue, survives_heavy_churn) {
    Queue<int> queue;
    int expected = 0;
    for (int round = 0; round < 200; ++round) {
        queue.enqueue(round);
        if (round % 3 == 0) {
            CHECK_EQ(queue.dequeue(), expected);
            ++expected;
        }
    }
    while (!queue.empty()) {
        CHECK_EQ(queue.dequeue(), expected);
        ++expected;
    }
    CHECK_EQ(expected, 200);
}

// ============================================================================
//  CircularBuffer
// ============================================================================

DAEDALUS_TEST(CircularBuffer, overwrite_policy_drops_the_oldest) {
    CircularBuffer<int> buffer(3);
    CHECK_FALSE(buffer.push(1));
    CHECK_FALSE(buffer.push(2));
    CHECK_FALSE(buffer.push(3));
    CHECK_TRUE(buffer.full());
    CHECK_TRUE(buffer.push(4));   // evicts 1
    CHECK_EQ(buffer.toVector(), (std::vector<int>{2, 3, 4}));
    CHECK_EQ(buffer.size(), 3u);
}

DAEDALUS_TEST(CircularBuffer, reject_policy_throws_when_full) {
    CircularBuffer<int> buffer(2, OverflowPolicy::Reject);
    buffer.push(1);
    buffer.push(2);
    CHECK_THROWS_AS(buffer.push(3), CapacityExceeded);
    CHECK_EQ(buffer.toVector(), (std::vector<int>{1, 2}));
}

DAEDALUS_TEST(CircularBuffer, pop_and_access) {
    CircularBuffer<int> buffer(4);
    for (int i = 1; i <= 4; ++i) buffer.push(i);
    CHECK_EQ(buffer.pop(), 1);
    CHECK_EQ(buffer.front(), 2);
    CHECK_EQ(buffer.back(), 4);
    CHECK_EQ(buffer.at(1), 3);
    CHECK_THROWS_AS(buffer.at(3), IndexOutOfRange);
    CHECK_THROWS_AS(CircularBuffer<int>(0), InvalidArgument);
}

DAEDALUS_TEST(CircularBuffer, erase_compacts_the_window) {
    CircularBuffer<int> buffer(4);
    for (int i = 1; i <= 4; ++i) buffer.push(i);
    CHECK_TRUE(buffer.erase(2));
    CHECK_EQ(buffer.toVector(), (std::vector<int>{1, 3, 4}));
    CHECK_FALSE(buffer.erase(42));
}

DAEDALUS_TEST(CircularBuffer, releases_all_elements) {
    Tracked::resetCounters();
    {
        CircularBuffer<Tracked> buffer(5);
        for (int i = 0; i < 30; ++i) buffer.push(Tracked(i));
        CHECK_EQ(Tracked::liveCount, 5);   // evictions must destroy, not leak
    }
    CHECK_EQ(Tracked::liveCount, 0);
}

// ============================================================================
//  SkipList
// ============================================================================

DAEDALUS_TEST(SkipList, keeps_keys_sorted_and_unique) {
    SkipList<int> list;
    for (int value : {5, 1, 9, 3, 7, 5, 1}) list.insert(value);
    CHECK_EQ(list.toVector(), (std::vector<int>{1, 3, 5, 7, 9}));
    CHECK_EQ(list.size(), 5u);
}

DAEDALUS_TEST(SkipList, search_and_erase) {
    SkipList<int> list;
    for (int i = 0; i < 200; ++i) list.insert(i * 2);
    CHECK_TRUE(list.contains(0));
    CHECK_TRUE(list.contains(398));
    CHECK_FALSE(list.contains(1));
    CHECK_TRUE(list.erase(100));
    CHECK_FALSE(list.contains(100));
    CHECK_FALSE(list.erase(100));
    CHECK_EQ(list.size(), 199u);
}

DAEDALUS_TEST(SkipList, min_max_and_lower_bound) {
    SkipList<int> list;
    CHECK_FALSE(list.minimum().has_value());
    CHECK_FALSE(list.maximum().has_value());
    for (int value : {10, 20, 30, 40}) list.insert(value);
    CHECK_EQ(list.minimum().value(), 10);
    CHECK_EQ(list.maximum().value(), 40);
    CHECK_EQ(list.lowerBound(25).value(), 30);
    CHECK_EQ(list.lowerBound(30).value(), 30);
    CHECK_FALSE(list.lowerBound(41).has_value());
}

DAEDALUS_TEST(SkipList, height_stays_logarithmic) {
    SkipList<int> list;
    for (int i = 0; i < 4096; ++i) list.insert(i);
    CHECK_EQ(list.size(), 4096u);
    // A perfectly balanced 4096-key structure is 12 levels deep; the coin
    // flips add slack but must not approach n.
    CHECK_LE(list.height(), 24);
    CHECK_EQ(list.toVector().front(), 0);
    CHECK_EQ(list.toVector().back(), 4095);
}

DAEDALUS_TEST(SkipList, erase_everything_then_reuse) {
    SkipList<int> list;
    for (int i = 0; i < 100; ++i) list.insert(i);
    for (int i = 0; i < 100; ++i) CHECK_TRUE(list.erase(i));
    CHECK_TRUE(list.empty());
    CHECK_EQ(list.height(), 0);
    list.insert(42);
    CHECK_EQ(list.toVector(), (std::vector<int>{42}));
}

DAEDALUS_TEST(SkipList, copy_is_independent) {
    SkipList<int> original;
    for (int i = 0; i < 20; ++i) original.insert(i);
    SkipList<int> copy = original;
    copy.insert(999);
    CHECK_EQ(original.size(), 20u);
    CHECK_EQ(copy.size(), 21u);
    CHECK_FALSE(original.contains(999));
}

DAEDALUS_TEST(SkipList, works_with_strings) {
    SkipList<std::string> list;
    for (const char* word : {"pear", "apple", "fig"}) list.insert(word);
    CHECK_EQ(list.toVector(),
             (std::vector<std::string>{"apple", "fig", "pear"}));
}

#pragma once
#include <atomic>
#include <llob/concepts.hpp>

namespace llob
{

template <SlotType T, std::size_t Capacity>
requires IsPowerOfTwo<Capacity>
class SPSCQueue
{
public:
    SPSCQueue() : producer_index_(0), consumer_index_(0) {}

    bool push(const T& item)
    {
        auto current_producer = producer_index_.load(std::memory_order_relaxed);
        auto current_consumer = consumer_index_.load(std::memory_order_acquire);
        if ((current_producer - current_consumer) >= Capacity)
            return false;
        buffer_[current_producer & (Capacity - 1)] = item;
        producer_index_.store(current_producer + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item)
    {
        auto current_consumer = consumer_index_.load(std::memory_order_relaxed);
        auto current_producer = producer_index_.load(std::memory_order_acquire);
        if (current_consumer == current_producer)
            return false;
        item = buffer_[current_consumer & (Capacity - 1)];
        consumer_index_.store(current_consumer + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const
    {
        auto current_consumer = consumer_index_.load(std::memory_order_relaxed);
        auto current_producer = producer_index_.load(std::memory_order_relaxed);
        return current_consumer == current_producer;
    }

private:
    alignas(64) T buffer_[Capacity];
    alignas(64) std::atomic<std::size_t> producer_index_;
    alignas(64) std::atomic<std::size_t> consumer_index_;
};

} // namespace llob

#pragma once
#include <llob/spsc_queue.hpp>
#include <array>
#include <cstddef>

namespace llob
{

// N-SPSC MPSC: one SPSCQueue per producer, consumer polls in priority order.
// Priority order is determined by the order queues are registered.
// Starvation guard: after StarvationThreshold consecutive high-priority
// dequeues, the consumer forces one poll of all lower-priority queues.

template <SlotType T, std::size_t Capacity, std::size_t NumProducers, std::size_t StarvationThreshold = 64>
class MPSCQueue
{
public:
    MPSCQueue() : queues_(), pop_count_(), last_served_() {}

    struct ProducerHandle {
        bool push(const T& item) const {
            return queue_->push(item);
        }

        private: 
            friend class MPSCQueue;
            explicit ProducerHandle(SPSCQueue<T, Capacity>* q) : queue_(q) {}
            SPSCQueue<T, Capacity>* queue_;
    };

    [[nodiscard]] ProducerHandle get_producer(std::size_t id) {
        return ProducerHandle{&queues_[id]};
    }

    [[nodiscard]] bool pop(T& item) {
        auto force_idx = NumProducers;
        for (auto i = 1; i < NumProducers; i++) {
            auto staleness = pop_count_ - last_served_[i];
            if (staleness >= StarvationThreshold && !queues_[i].empty()) {
                if (force_idx == NumProducers || staleness > pop_count_ - last_served_[force_idx]) {
                    force_idx = i;
                }
            }
        }

        if (force_idx != NumProducers) {
            auto is_force_popped_successful = queues_[force_idx].pop(item);
            if (is_force_popped_successful) {
                last_served_[force_idx] = pop_count_;
                ++pop_count_;
                return true;
            }
        }

        for (auto i = 0; i < NumProducers; i++) {
            if (!queues_[i].empty()) {
                auto is_priority_popped_successful = queues_[i].pop(item);
                if (is_priority_popped_successful) {
                    last_served_[i] = pop_count_;
                    ++pop_count_;
                    return true;
                }
            }
        }

        return false;
    }
    
private:
    std::array<SPSCQueue<T, Capacity>, NumProducers> queues_;
    std::size_t pop_count_;
    std::array<std::size_t, NumProducers> last_served_;
};

} // namespace llob

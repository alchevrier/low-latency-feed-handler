#pragma once
#include <llob/market_data_event.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>

namespace llob
{

// SOA order book for a single symbol.
// Separate sorted int64_t price-tick arrays per side.
// Bids: sorted descending — bids_[0] is best bid.
// Asks: sorted ascending  — asks_[0] is best ask.
// Synchronised via seqlock for concurrent reader (matching thread).

template <std::size_t MaxLevels>
class OrderBook
{
public:
    void add(const MarketDataEvent& e) {
        seq_.fetch_add(1, std::memory_order_release);

        if (e.side == Side::Bid) {
            if (bid_count_ >= MaxLevels) {
                auto worstPrice = bid_prices_[MaxLevels - 1];
                if (worstPrice > e.price) {
                    seq_.fetch_add(1, std::memory_order_release);
                    return;
                }
                bid_count_ = MaxLevels - 1;
            }

            int64_t* prices = bid_prices_.data();
            int64_t* qtys = bid_qtys_.data();
            auto insertPoint = std::lower_bound(prices, prices + bid_count_, e.price, std::greater<int64_t>{});
            auto idx = insertPoint - prices;

            std::memmove(prices + idx + 1, prices + idx, (bid_count_ - idx) * sizeof(int64_t));
            std::memmove(qtys + idx + 1, qtys + idx, (bid_count_ - idx) * sizeof(int64_t));

            prices[idx] = e.price;
            qtys[idx] = e.qty;

            bid_count_++;
        } else {
            if (ask_count_ >= MaxLevels) {
                auto worstPrice = ask_prices_[MaxLevels - 1];
                if (worstPrice < e.price) {
                    seq_.fetch_add(1, std::memory_order_release);
                    return;
                }
                ask_count_ = MaxLevels - 1;
            }

            int64_t* prices = ask_prices_.data();
            int64_t* qtys = ask_qtys_.data();
            auto insertPoint = std::lower_bound(prices, prices + ask_count_, e.price);
            auto idx = insertPoint - prices;

            std::memmove(prices + idx + 1, prices + idx, (ask_count_ - idx) * sizeof(int64_t));
            std::memmove(qtys + idx + 1, qtys + idx, (ask_count_ - idx) * sizeof(int64_t));

            prices[idx] = e.price;
            qtys[idx] = e.qty;

            ask_count_++;
        }

        seq_.fetch_add(1, std::memory_order_release);
    }
    
    int64_t best_bid() const {
        while (true) {
            auto s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1) continue;
            auto price = bid_prices_[0];
            auto s2 = seq_.load(std::memory_order_acquire);
            if (s1 == s2) {
                if (bid_count_ == 0) {
                    return std::numeric_limits<int64_t>::min();
                } else {
                    return price;
                }
            }
        }
    }

    int64_t best_ask() const {
        while (true) {
            auto s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1) continue;
            auto price = ask_prices_[0];
            auto s2 = seq_.load(std::memory_order_acquire);
            if (s1 == s2) {
                if (ask_count_ == 0) {
                    return std::numeric_limits<int64_t>::max();
                } else {
                    return price;
                }
            }
        }
    }
private:
    std::array<int64_t, MaxLevels> bid_prices_;
    std::array<int64_t, MaxLevels> bid_qtys_;
    std::array<int64_t, MaxLevels> ask_prices_;
    std::array<int64_t, MaxLevels> ask_qtys_;
    std::size_t bid_count_{0};
    std::size_t ask_count_{0};
    std::atomic<std::size_t> seq_{0};  // seqlock counter
};

} // namespace llob

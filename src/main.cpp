#include <atomic>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <llob/mpsc_queue.hpp>
#include <llob/market_data_event.hpp>
#include <llob/order_book.hpp>
#include <itch/mold_udp64.hpp>
#include <itch/itch_parser.hpp>

struct PipelineCtx {
    llob::MPSCQueue<llob::MarketDataEvent, 1024, 1> mpsc;
    std::atomic<bool> stop{false};
};

constexpr uint32_t kRxLcore          = 1;  // CPU 4 per coremask -l 2,4,6
constexpr uint32_t kBookWriterLcore  = 2;  // CPU 6

int rx_parse_loop(void* args);
int book_writer_loop(void* args);

int main(int argc, char* argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "EAL init failure\n");
    }
    argc -= ret; // strip EAL args — remaining are app-specific
    argv += ret;

    constexpr uint16_t NUM_MBUFS = 8191; // power of 2 minus 1
    constexpr uint16_t MBUF_CACHE_SIZE = 256;

    rte_mempool* mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL", NUM_MBUFS, MBUF_CACHE_SIZE,
        0, // private data size
        RTE_MBUF_DEFAULT_BUF_SIZE, // 2048 + headroom
        rte_socket_id()
    );

    if (!mbuf_pool) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    struct rte_eth_conf port_conf{};
    uint16_t nb_rxd = 512;

    rte_eth_dev_configure(0, 1, 0, &port_conf);  // port 0, 1 RX queue, 0 TX
    rte_eth_dev_adjust_nb_rx_tx_desc(0, &nb_rxd, nullptr); 

    rte_eth_rx_queue_setup(
        0, 0, // port 0, queue 0
        nb_rxd,
        rte_eth_dev_socket_id(0),
        nullptr,
        mbuf_pool
    );

    rte_eth_dev_start(0);

    PipelineCtx ctx{};
    rte_eal_remote_launch(rx_parse_loop, &ctx, kRxLcore);
    rte_eal_remote_launch(book_writer_loop, &ctx, kBookWriterLcore);
    rte_eal_mp_wait_lcore();

    rte_eth_dev_stop(0);
    rte_eth_dev_close(0);
    rte_eal_cleanup();
    
    return 0;
}

int rx_parse_loop(void* args) {
    auto* ctx = static_cast<PipelineCtx*>(args);

    constexpr uint16_t BURST_SIZE = 32;
    rte_mbuf* mbufs[BURST_SIZE];

    while (!ctx->stop.load(std::memory_order_relaxed)) {
        uint16_t nb_rx = rte_eth_rx_burst(0, 0, mbufs, BURST_SIZE);
        for (uint16_t i = 0; i < nb_rx; ++i) {
            rte_mbuf* mbuf = mbufs[i];
            constexpr uint16_t kL2L3L4Len = 42;
            const uint8_t* data = rte_pktmbuf_mtod(mbuf, const uint8_t*) + kL2L3L4Len;
            uint16_t mold_len = mbuf->data_len - kL2L3L4Len;

            itch::unwrap(data, mold_len, [&ctx](const uint8_t* msg, uint16_t msg_len, uint64_t) {
                if (msg[0] == 'A')
                    ctx->mpsc.get_producer(0).push(itch::parse_add_order(msg, msg_len));
            });
            rte_pktmbuf_free(mbuf);
        }
    }

    return 0;
}

int book_writer_loop(void* args) {
    auto* ctx = static_cast<PipelineCtx*>(args);

    constexpr int N = 50;
    llob::OrderBook<N> order_book;

    while (!ctx->stop.load(std::memory_order_relaxed)) {
        llob::MarketDataEvent event;
        while (ctx->mpsc.pop(event)) {
            order_book.add(event);
        }
    }

    return 0;
}
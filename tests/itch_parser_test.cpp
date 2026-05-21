#include <gtest/gtest.h> 
#include <itch/itch_parser.hpp>

constexpr uint8_t ADD_ORDER_TEST[] = {
    'A', 
    0x04, 0x1A, 
    0x82, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 
    'B', 
    0x00, 0x00, 0x02, 0x58,
    'T', 'E', 'S', 'T', 'Q', 'A', 'S', 'I', 
    0x00, 0x0F, 0x42, 0x40 
};

TEST(ParseAddOrder, ParseAddOrder) {
    auto event = itch::parse_add_order(ADD_ORDER_TEST, sizeof(ADD_ORDER_TEST));
    EXPECT_EQ(event.price, 1000000);
    EXPECT_EQ(event.qty, 600);
    EXPECT_EQ(event.order_id, 200);
    EXPECT_EQ(event.stock_locate, 1050);
    EXPECT_EQ(event.side, llob::Side::Bid);
    EXPECT_EQ(event.event_type, llob::EventType::Add);
}

TEST(ParseAddOrder, RejectsTooShortMessage) {
    EXPECT_DEATH(itch::parse_add_order(ADD_ORDER_TEST, 35), "");
}
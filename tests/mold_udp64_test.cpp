#include <gtest/gtest.h> 
#include <itch/mold_udp64.hpp>

constexpr uint8_t MESSAGE_SMALLER_HEADER[] = {
    'O', 'N', 'L', 'Y', 'P', 'R', 'E', 'S', 'E', 'N'
};

constexpr uint8_t MESSAGE_VALID[] = {
    'O', 'N', 'L', 'Y', 'P', 'R', 'E', 'S', 'E', 'N',
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x01,
    0x00, 0x24,
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

constexpr uint8_t MULTIPLE_MESSAGE_VALID[] = {
    'O', 'N', 'L', 'Y', 'P', 'R', 'E', 'S', 'E', 'N',
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x03,
    0x00, 0x24,
    'A', 
    0x04, 0x1A, 
    0x82, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 
    'B', 
    0x00, 0x00, 0x02, 0x58,
    'T', 'E', 'S', 'T', 'Q', 'A', 'S', 'I', 
    0x00, 0x0F, 0x42, 0x40 ,
    0x00, 0x24,
    'A', 
    0x04, 0x1A, 
    0x82, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 
    'B', 
    0x00, 0x00, 0x02, 0x58,
    'T', 'E', 'S', 'T', 'Q', 'A', 'S', 'I', 
    0x00, 0x0F, 0x42, 0x40,
    0x00, 0x24,
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

TEST(Unwrap, MessageSmallerThanHeaderLength) {
    EXPECT_DEATH(itch::unwrap(MESSAGE_SMALLER_HEADER, 10, [](const uint8_t*, uint16_t, uint64_t) {}), "");
}

TEST(Unwrap, MessageExceedingDatagramLength) {
    EXPECT_DEATH(itch::unwrap(MESSAGE_VALID, 32, [](const uint8_t*, uint16_t, uint64_t) {}), "");
}

TEST(Unwrap, MessageLengthExceedingDatagramLength) {
    EXPECT_DEATH(itch::unwrap(MESSAGE_VALID, sizeof(MESSAGE_VALID) - 20, [](const uint8_t*, uint16_t, uint64_t) {}), "");
}

TEST(Unwrap, MessageSuccessfullyUnwrapped) {
    itch::unwrap(MESSAGE_VALID, sizeof(MESSAGE_VALID), [](const uint8_t* cursor, uint16_t msg_len, uint64_t seq_no) {
        EXPECT_EQ(cursor, MESSAGE_VALID + 22);
        EXPECT_EQ(msg_len, 36);
        EXPECT_EQ(seq_no, 1);
    });
}

TEST(Unwrap, MessageSuccessfullyUnwrappedMultiple) {
    uint64_t idx = 0;

    itch::unwrap(MULTIPLE_MESSAGE_VALID, sizeof(MULTIPLE_MESSAGE_VALID), [&idx](const uint8_t* cursor, uint16_t msg_len, uint64_t seq_no) {
        EXPECT_EQ(cursor, MULTIPLE_MESSAGE_VALID + 22 + 38 * idx);
        EXPECT_EQ(msg_len, 36);
        EXPECT_EQ(seq_no, 1 + idx);
        ++idx;
    });
}
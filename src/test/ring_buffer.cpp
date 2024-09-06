#include "cfgo/ring_buffer.hpp"
#include "gtest/gtest.h"

TEST(RingBuffer, Full) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 1, 1);
    ASSERT_FALSE(rb0.full());
    ASSERT_TRUE(rb0.enqueue(1, false));
    ASSERT_TRUE(rb0.full());
    int out;
    ASSERT_TRUE(rb0.dequeue(out));
    ASSERT_FALSE(rb0.full());

    AdaptiveRingBuffer<int> rb1(2, 2, 2);
    ASSERT_FALSE(rb1.full());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_TRUE(rb1.full());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_FALSE(rb1.full());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_FALSE(rb1.full());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_FALSE(rb1.full());

    AdaptiveRingBuffer<int> rb2(2, 3, 2);
    ASSERT_FALSE(rb2.full());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.full());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.full());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.full());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.full());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.full());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.full());
}

TEST(RingBuffer, Empty) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 1, 1);
    ASSERT_TRUE(rb0.empty());
    ASSERT_TRUE(rb0.enqueue(1, false));
    ASSERT_FALSE(rb0.empty());
    int out;
    ASSERT_TRUE(rb0.dequeue(out));
    ASSERT_TRUE(rb0.empty());

    AdaptiveRingBuffer<int> rb1(2, 2, 2);
    ASSERT_TRUE(rb1.empty());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_FALSE(rb1.empty());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_FALSE(rb1.empty());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_FALSE(rb1.empty());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_FALSE(rb1.empty());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_FALSE(rb1.empty());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_TRUE(rb1.empty());

    AdaptiveRingBuffer<int> rb2(2, 3, 2);
    ASSERT_TRUE(rb2.empty());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_FALSE(rb2.empty());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_TRUE(rb2.empty());
}

TEST(RingBuffer, Size) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 1, 1);
    ASSERT_EQ(0, rb0.size());
    ASSERT_TRUE(rb0.enqueue(1, false));
    ASSERT_EQ(1, rb0.size());
    int out;
    ASSERT_TRUE(rb0.dequeue(out));
    ASSERT_EQ(0, rb0.size());

    AdaptiveRingBuffer<int> rb1(2, 2, 2);
    ASSERT_EQ(0, rb1.size());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_EQ(1, rb1.size());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_EQ(2, rb1.size());
    ASSERT_TRUE(rb1.enqueue(1, false));
    ASSERT_EQ(3, rb1.size());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_EQ(2, rb1.size());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_EQ(1, rb1.size());
    ASSERT_TRUE(rb1.dequeue(out));
    ASSERT_EQ(0, rb1.size());

    AdaptiveRingBuffer<int> rb2(2, 3, 2);
    ASSERT_EQ(0, rb2.size());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_EQ(1, rb2.size());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_EQ(2, rb2.size());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_EQ(3, rb2.size());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_EQ(4, rb2.size());
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_EQ(5, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(4, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(3, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(2, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(1, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(0, rb2.size());
}

TEST(RingBuffer, EnqueueAndDequeue) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 1, 1);
    ASSERT_TRUE(rb0.enqueue(1, false));
    ASSERT_EQ(1, rb0.size());
    ASSERT_FALSE(rb0.enqueue(2, false));
    ASSERT_EQ(1, rb0.size());
    int out;
    ASSERT_TRUE(rb0.dequeue(out));
    ASSERT_EQ(1, out);
    ASSERT_EQ(0, rb0.size());
    ASSERT_FALSE(rb0.dequeue(out));
    ASSERT_EQ(0, rb0.size());
    ASSERT_TRUE(rb0.enqueue(3, false));
    ASSERT_EQ(1, rb0.size());
    ASSERT_EQ(3, *rb0.queue_head());
    ASSERT_FALSE(rb0.enqueue(4, false));
    ASSERT_EQ(1, rb0.size());
    ASSERT_EQ(3, *rb0.queue_head());
    ASSERT_TRUE(rb0.enqueue(4, true));
    ASSERT_EQ(1, rb0.size());
    ASSERT_EQ(4, *rb0.queue_head());
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

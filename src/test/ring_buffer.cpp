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
    int out;
    AdaptiveRingBuffer<int> rb0(2, 1, 1);
    ASSERT_TRUE(rb0.enqueue(1, false));
    ASSERT_EQ(1, rb0.size());
    ASSERT_FALSE(rb0.enqueue(2, false));
    ASSERT_EQ(1, rb0.size());
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

    AdaptiveRingBuffer<int> rb2(2, 3, 2);
    ASSERT_TRUE(rb2.enqueue(1, false));
    ASSERT_TRUE(rb2.enqueue(2, false));
    ASSERT_TRUE(rb2.enqueue(3, false));
    ASSERT_TRUE(rb2.enqueue(4, false));
    ASSERT_TRUE(rb2.enqueue(5, false));
    ASSERT_EQ(1, *rb2.queue_head());
    ASSERT_EQ(5, rb2.size());
    ASSERT_FALSE(rb2.enqueue(6, false));
    ASSERT_EQ(1, *rb2.queue_head());
    ASSERT_EQ(5, rb2.size());
    ASSERT_TRUE(rb2.enqueue(6, true));
    ASSERT_EQ(2, *rb2.queue_head());
    ASSERT_EQ(5, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(2, out);
    ASSERT_EQ(3, *rb2.queue_head());
    ASSERT_EQ(4, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(3, out);
    ASSERT_EQ(4, *rb2.queue_head());
    ASSERT_EQ(3, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(4, out);
    ASSERT_EQ(5, *rb2.queue_head());
    ASSERT_EQ(2, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(5, out);
    ASSERT_EQ(6, *rb2.queue_head());
    ASSERT_EQ(1, rb2.size());
    ASSERT_TRUE(rb2.dequeue(out));
    ASSERT_EQ(6, out);
    ASSERT_EQ(nullptr, rb2.queue_head());
    ASSERT_EQ(0, rb2.size());
}

TEST(RingBuffer, Capacity) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 3, 2);
    // ->
    ASSERT_EQ(0, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(1, false));
    //    o i 
    // -> 1 x
    ASSERT_EQ(1, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(2, false));
    //    o      i
    // -> 1 2 -> x
    ASSERT_EQ(3, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(3, false));
    //    o        i
    // -> 1 2 -> 3 x
    ASSERT_EQ(3, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(4, false));
    //    o             i
    // -> 1 2 -> 3 4 -> x
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(5, false));
    //    o               i
    // -> 1 2 -> 3 4 -> 5 x
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_FALSE(rb0.enqueue(6, false));
    //    o               i
    // -> 1 2 -> 3 4 -> 5 x
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_TRUE(rb0.enqueue(6, true));
    //    i o
    // -> x 2 -> 3 4 -> 5 6
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_TRUE(rb0.dequeue());
    //    i      o
    // -> x x -> 3 4 -> 5 6
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_TRUE(rb0.dequeue());
    //    i        o
    // -> x x -> x 4 -> 5 6
    ASSERT_EQ(5, rb0.capacity());
    ASSERT_TRUE(rb0.dequeue());
    //    i             o
    // -> x x -> x x -> 5 6
    //    i      o
    // -> x x -> 5 6
    ASSERT_EQ(3, rb0.capacity());
    ASSERT_TRUE(rb0.dequeue());
    //    i        o
    // -> x x -> x 6
    ASSERT_EQ(3, rb0.capacity());
    ASSERT_TRUE(rb0.dequeue());
    //    i|o        
    // -> x x -> x x
    ASSERT_EQ(3, rb0.capacity());

    AdaptiveRingBuffer<int> rb1(2, 3, 1);
    // ->
    ASSERT_EQ(0, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(1, false));
    //    o i 
    // -> 1 x
    ASSERT_EQ(1, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(2, false));
    //    o      i
    // -> 1 2 -> x
    ASSERT_EQ(3, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(3, false));
    //    o        i
    // -> 1 2 -> 3 x
    ASSERT_EQ(3, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(4, false));
    //    o             i
    // -> 1 2 -> 3 4 -> x
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(5, false));
    //    o               i
    // -> 1 2 -> 3 4 -> 5 x
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_FALSE(rb1.enqueue(6, false));
    //    o               i
    // -> 1 2 -> 3 4 -> 5 x
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_TRUE(rb1.enqueue(6, true));
    //    i o
    // -> x 2 -> 3 4 -> 5 6
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_TRUE(rb1.dequeue());
    //    i      o
    // -> x x -> 3 4 -> 5 6
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_TRUE(rb1.dequeue());
    //    i        o
    // -> x x -> x 4 -> 5 6
    ASSERT_EQ(5, rb1.capacity());
    ASSERT_TRUE(rb1.dequeue());
    //    i             o
    // -> x x -> x x -> 5 6
    //    i      o
    // -> x x -> 5 6
    ASSERT_EQ(3, rb1.capacity());
    ASSERT_TRUE(rb1.dequeue());
    //    i        o
    // -> x x -> x 6
    ASSERT_EQ(3, rb1.capacity());
    ASSERT_TRUE(rb1.dequeue());
    //    i|o        
    // -> x   x -> x x
    //    i|o
    // -> x   x
    ASSERT_EQ(1, rb1.capacity());
}

TEST(RingBuffer, Loop) {
    using namespace cfgo;
    AdaptiveRingBuffer<int> rb0(2, 3, 2);
    rb0.enqueue(1, false);
    rb0.enqueue(2, false);
    rb0.enqueue(3, false);
    rb0.enqueue(4, false);
    rb0.enqueue(5, false); // -> 1 2 3 4 5
    auto i = 0;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(5, i);
    rb0.enqueue(6, true); // -> 2 3 4 5 6
    i = 1;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(6, i);
    rb0.enqueue(7, true); // -> 3 4 5 6 7
    i = 2;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
    rb0.dequeue(); // -> 4 5 6 7
    i = 3;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
    rb0.dequeue(); // -> 5 6 7
    i = 4;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
    rb0.dequeue(); // -> 6 7
    i = 5;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
    rb0.dequeue(); // -> 7
    i = 6;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
    rb0.dequeue(); // -> 
    i = 7;
    for (auto & v : rb0) {
        ASSERT_EQ(++i, v);
    }
    ASSERT_EQ(7, i);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

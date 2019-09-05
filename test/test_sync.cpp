#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <array>
#include <numeric>   // std::accumulate
#include "sync.h"

using namespace std;
using namespace srt::sync;

TEST(SyncDuration, BasicChecks)
{
    const steady_clock::duration d;

    EXPECT_EQ(d.count(), 0);
    EXPECT_TRUE (d == d);   // operator==
    EXPECT_FALSE(d != d);   // operator!=
    EXPECT_EQ(d, steady_clock::duration::zero());
    EXPECT_EQ(d, from_microseconds(0));
    EXPECT_EQ(d, from_milliseconds(0));
    EXPECT_EQ(d, from_seconds(0));
    EXPECT_EQ(to_milliseconds(d), 0);
    EXPECT_EQ(to_microseconds(d), 0);
    EXPECT_EQ(to_seconds(d), 0);

    const steady_clock::duration a = d + from_milliseconds(120);
    EXPECT_EQ(a, from_milliseconds(120));
    EXPECT_EQ(to_milliseconds(a), 120);
    EXPECT_EQ(to_microseconds(a), 120000);
    EXPECT_EQ(to_seconds(a),           0);

}

TEST(SyncDuration, Operators)
{
    const steady_clock::duration a;

    EXPECT_EQ(a.count(), 0);
    EXPECT_TRUE (a == a);   // operator==
    EXPECT_FALSE(a != a);   // operator!=
    EXPECT_FALSE(a > a);    // operator>
    EXPECT_FALSE(a < a);    // operator<
    EXPECT_TRUE(a <= a);    // operator<=
    EXPECT_TRUE(a >= a);    // operator>=

    const steady_clock::duration b = a + from_milliseconds(120);
    EXPECT_FALSE(b == a);   // operator==
    EXPECT_TRUE (b != a);   // operator!=
    EXPECT_TRUE (b >  a);   // operator>
    EXPECT_FALSE(a >  b);   // operator>
    EXPECT_FALSE(b <  a);   // operator<
    EXPECT_TRUE (a <  b);   // operator<
    EXPECT_FALSE(b <= a);   // operator<=
    EXPECT_TRUE (a <= b);   // operator<=
    EXPECT_TRUE (b >= a);   // operator>=
    EXPECT_FALSE(a >= b);   // operator>=

    const steady_clock::duration c = steady_clock::duration(numeric_limits<int64_t>::max());
    EXPECT_EQ(c.count(), numeric_limits<int64_t>::max());
    const steady_clock::duration d = steady_clock::duration(numeric_limits<int64_t>::min());
    EXPECT_EQ(d.count(), numeric_limits<int64_t>::min());
}

TEST(SyncDuration, OperatorsMinus)
{
    const steady_clock::duration a = from_seconds(5);
    const steady_clock::duration b = from_milliseconds(3500);

    EXPECT_EQ(to_milliseconds(a - b), 1500);
    EXPECT_EQ(to_milliseconds(b - a), -1500);
}


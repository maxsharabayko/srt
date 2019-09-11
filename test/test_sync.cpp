#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <future>
#include <array>
#include <numeric>  // std::accumulate
#include <regex>    // Used in FormatTime test
#include "sync.h"

// This test set requires support for C++14
// * Uses "'" as a separator: 100'000
// * Uses operator"ms" at al from chrono

using namespace std;
using namespace srt::sync;

// GNUC supports C++14 starting from version 5
//#if defined(__GNUC__) && (__GNUC__ < 5)
////namespace srt
//constexpr chrono::milliseconds operator"" ms(
//    unsigned long long _Val) { // return integral milliseconds
//    return chrono::milliseconds(_Val);
//}
//#endif

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

TEST(SyncDuration, RelOperators)
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

TEST(SyncDuration, OperatorMinus)
{
    const steady_clock::duration a = from_seconds(5);
    const steady_clock::duration b = from_milliseconds(3500);

    EXPECT_EQ(to_milliseconds(a - b), 1500);
    EXPECT_EQ(to_milliseconds(b - a), -1500);
    EXPECT_EQ((a - a).count(), 0);
}

TEST(SyncDuration, OperatorMinusEq)
{
    const steady_clock::duration a = from_seconds(5);
    const steady_clock::duration b = from_milliseconds(3500);

    steady_clock::duration c = a;
    EXPECT_EQ(c, a);
    c -= b;
    EXPECT_EQ(to_milliseconds(c), 1500);
    c = b;
    EXPECT_EQ(c, b);
    c -= a;
    EXPECT_EQ(to_milliseconds(c), -1500);
}

TEST(SyncDuration, OperatorPlus)
{
    const steady_clock::duration a = from_seconds(5);
    const steady_clock::duration b = from_milliseconds(3500);

    EXPECT_EQ(to_milliseconds(a + b), 8500);
    EXPECT_EQ(to_milliseconds(b + a), 8500);
}

TEST(SyncDuration, OperatorPlusEq)
{
    const steady_clock::duration a = from_seconds(5);
    const steady_clock::duration b = from_milliseconds(3500);

    steady_clock::duration c = a;
    EXPECT_EQ(c, a);
    c += b;
    EXPECT_EQ(to_milliseconds(c), 8500);
    c = b;
    EXPECT_EQ(c, b);
    c += a;
    EXPECT_EQ(to_milliseconds(c), 8500);
}

TEST(SyncDuration, OperatorMultInt)
{
    const steady_clock::duration a = from_milliseconds(3500);

    EXPECT_EQ(to_milliseconds(a), 3500);
    EXPECT_EQ(to_milliseconds(a * 2), 7000);
}

TEST(SyncDuration, OperatorMultIntEq)
{
    steady_clock::duration a = from_milliseconds(3500);

    EXPECT_EQ(to_milliseconds(a), 3500);
    a *= 2;
    EXPECT_EQ(to_milliseconds(a), 7000);
}

/*****************************************************************************/
/*
 * TimePoint tests
*/
/*****************************************************************************/

TEST(SyncTimePoint, DefaultConstructorZero)
{
    steady_clock::time_point a;
    EXPECT_TRUE(is_zero(a));
}

TEST(SyncTimePoint, RelOperators)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    EXPECT_TRUE (a == a);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE (a != b);
    EXPECT_TRUE (a != b);

    EXPECT_TRUE (a >= a);
    EXPECT_FALSE(b >= a);
    EXPECT_TRUE (a >  b);
    EXPECT_FALSE(a >  a);
    EXPECT_TRUE (a <= a);
    EXPECT_TRUE (b <= a);
    EXPECT_FALSE(a <= b);
    EXPECT_FALSE(a <  a);
    EXPECT_TRUE (b <  a);
    EXPECT_FALSE(a <  b);
}

TEST(SyncTimePoint, OperatorMinus)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    EXPECT_EQ((a - b).count(), delta);
    EXPECT_EQ((b - a).count(), -delta);
}

TEST(SyncTimePoint, OperatorEq)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max() - delta);
    const steady_clock::time_point b = a;
    EXPECT_EQ(a, b);
}

TEST(SyncTimePoint, OperatorMinusPlusDuration)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);

    EXPECT_EQ((a + steady_clock::duration(-delta)), b);
    EXPECT_EQ((b + steady_clock::duration(+delta)), a);

    EXPECT_EQ((a - steady_clock::duration(+delta)), b);
    EXPECT_EQ((b - steady_clock::duration(-delta)), a);
}

TEST(SyncTimePoint, OperatorPlusEqDuration)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    steady_clock::time_point r = a;
    EXPECT_EQ(r, a);
    r += steady_clock::duration(-delta);
    EXPECT_EQ(r, b);
    r = b;
    EXPECT_EQ(r, b);
    r += steady_clock::duration(+delta);
    EXPECT_EQ(r, a);
    r = a;
    EXPECT_EQ(r, a);
    r -= steady_clock::duration(+delta);
    EXPECT_EQ((a - steady_clock::duration(+delta)), b);
    EXPECT_EQ((b - steady_clock::duration(-delta)), a);
}

TEST(SyncTimePoint, OperatorMinusEqDuration)
{
    const int64_t delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    steady_clock::time_point r = a;
    EXPECT_EQ(r, a);
    r -= steady_clock::duration(+delta);
    EXPECT_EQ(r, b);
    r = b;
    EXPECT_EQ(r, b);
    r -= steady_clock::duration(-delta);
    EXPECT_EQ(r, a);
}

/*****************************************************************************/
/*
 * SyncEvent tests
*/
/*****************************************************************************/

TEST(SyncEvent, WaitFor)
{
    SyncEvent e;
    for (int tout_us : { 50, 100, 500, 1000, 101000, 1001000 })
    {
        const steady_clock::duration timeout = from_microseconds(tout_us);
        const steady_clock::time_point start = steady_clock::now();
        EXPECT_FALSE(e.wait_for(timeout));
        const steady_clock::time_point stop = steady_clock::now();
        if (tout_us < 1000)
        {
            cerr << "SyncEvent::wait_for(" << to_microseconds(timeout) << "us) took "
                << to_microseconds(stop - start) << "us" << endl;
        }
        else
        {
            cerr << "SyncEvent::wait_until(" << to_milliseconds(timeout) << " ms) took "
                << to_microseconds(stop - start) / 1000.0 << " ms" << endl;
        }
    }
}

TEST(SyncEvent, WaitForNotifyOne)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(5);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        return e->wait_for(timeout);
    };
    auto wait_async_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    e.notify_one();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::ready);
    const int wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res);
}

TEST(SyncEvent, WaitNotifyOne)
{
    SyncEvent e;

    auto wait_async = [](SyncEvent* e) {
        return e->wait();
    };
    auto wait_async_res = async(launch::async, wait_async, &e);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    e.notify_one();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::ready);
    wait_async_res.get();
}

TEST(SyncEvent, WaitForTwoNotifyOne)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(3);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        return e->wait_for(timeout);
    };
    auto wait_async1_res = async(launch::async, wait_async, &e, timeout);
    auto wait_async2_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async1_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    EXPECT_EQ(wait_async2_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    e.notify_one();
    // Now only one waiting thread should become ready
    const future_status status1 = wait_async1_res.wait_for(chrono::milliseconds(100));
    const future_status status2 = wait_async2_res.wait_for(chrono::milliseconds(100));

    const bool isready1 = (status1 == future_status::ready);
    EXPECT_EQ(status1, isready1 ? future_status::ready : future_status::timeout);
    EXPECT_EQ(status2, isready1 ? future_status::timeout : future_status::ready);

    // Expect one thread to be woken up by condition
    EXPECT_TRUE (isready1 ? wait_async1_res.get() : wait_async2_res.get());
    // Expect timeout on another thread
    EXPECT_FALSE(isready1 ? wait_async2_res.get() : wait_async1_res.get());
}

TEST(SyncEvent, WaitForTwoNotifyAll)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(3);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        return e->wait_for(timeout);
    };
    auto wait_async1_res = async(launch::async, wait_async, &e, timeout);
    auto wait_async2_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async1_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    EXPECT_EQ(wait_async2_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    e.notify_all();
    // Now only one waiting thread should become ready
    const future_status status1 = wait_async1_res.wait_for(chrono::milliseconds(100));
    const future_status status2 = wait_async2_res.wait_for(chrono::milliseconds(100));
    EXPECT_EQ(status1, future_status::ready);
    EXPECT_EQ(status2, future_status::ready);
    // Expect both threads to wake up by condition
    EXPECT_TRUE(wait_async1_res.get());
    EXPECT_TRUE(wait_async2_res.get());
}

TEST(SyncEvent, WaitForNotifyAll)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(5);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration &timeout) {
        return e->wait_for(timeout);
    };
    auto wait_async_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.notify_all();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::ready);
    const int wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res);
}

TEST(SyncEvent, WaitUntil)
{
    SyncEvent e;
    for (int tout_us : { 50, 100, 500, 1000, 101000, 1001000 })
    {
        const steady_clock::duration timeout = from_microseconds(tout_us);
        const steady_clock::time_point start = steady_clock::now();
        EXPECT_TRUE(e.wait_until(start + timeout));
        const steady_clock::time_point stop = steady_clock::now();
        if (tout_us < 1000)
        {
            cerr << "SyncEvent::wait_until(" << to_microseconds(timeout) << " us) took "
                << to_microseconds(stop - start) << " us" << endl;
        }
        else
        {
            cerr << "SyncEvent::wait_until(" << to_milliseconds(timeout) << " ms) took "
                << to_microseconds(stop - start)/1000.0 << " ms" << endl;
        }
    }
}

TEST(SyncEvent, WaitUntilInterrupt)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(5);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        const steady_clock::time_point start = steady_clock::now();
        const int res = e->wait_until(start + timeout);
        return res;
    };
    auto wait_async_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.interrupt();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::ready);
    const bool wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res);
}

TEST(SyncEvent, WaitUntilNotifyOne)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(5);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        const steady_clock::time_point start = steady_clock::now();
        const int res = e->wait_until(start + timeout);
        return res;
    };
    auto wait_async_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.notify_one();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.interrupt();
    const bool wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res);
}

TEST(SyncEvent, WaitUntilNotifyAll)
{
    SyncEvent e;
    const steady_clock::duration timeout = from_seconds(5);

    auto wait_async = [](SyncEvent* e, const steady_clock::duration& timeout) {
        const steady_clock::time_point start = steady_clock::now();
        const int res = e->wait_until(start + timeout);
        return res;
    };
    auto wait_async_res = async(launch::async, wait_async, &e, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.notify_all();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    e.interrupt();
    const bool wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res);
}

/*****************************************************************************/
/*
 * FormatTime
*/
/*****************************************************************************/

TEST(Sync, FormatTime)
{
    const steady_clock::time_point a = steady_clock::now();
    const string time1 = FormatTime(a);
    const string time2 = FormatTime(a);
    cerr << "Same time formated twice: " << time1 << " and " << time2 << endl;
    //EXPECT_TRUE(time1 == time2);
    regex rex("[[:digit:]]{2}\:[[:digit:]]{2}\:[[:digit:]]{2}\.[[:digit:]]{6}");
    cerr << "regex_match: " << regex_match(time1, rex) << endl;
    
    cerr << FormatTime(a) << endl;
    cerr << FormatTime(a + from_seconds(1)) << endl;
}


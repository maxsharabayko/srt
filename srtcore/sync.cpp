#include <iomanip>
#include <stdexcept>
#include "sync.h"
#include "udt.h"
#include "srt_compat.h"

#ifndef USE_STL_CHRONO
#if defined(_WIN32)
#define TIMING_USE_QPC
#include "win/wintime.h"
#elif defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
#define TIMING_USE_MACH_ABS_TIME
#include <mach/mach_time.h>
#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_TIMERS > 0
#define TIMING_USE_MONOTONIC_CLOCK
#endif

namespace srt
{
namespace sync
{

int64_t get_cpu_frequency()
{
    int64_t frequency = 1; // 1 tick per microsecond.

#if defined(TIMING_USE_QPC)

    LARGE_INTEGER ccf; // in counts per second
    if (QueryPerformanceFrequency(&ccf))
        frequency = ccf.QuadPart / 1000000; // counts per microsecond

#elif defined(TIMING_USE_MACH_ABS_TIME)

    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    frequency = info.denom * int64_t(1000) / info.numer;

#elif defined(IA32) || defined(IA64) || defined(AMD64)
    uint64_t t1, t2;

    rdtsc(t1);
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;
    nanosleep(&ts, NULL);
    rdtsc(t2);

    // CPU clocks per microsecond
    frequency = int64_t(t2 - t1) / 100000;
#endif

    return frequency;
}

static const int64_t s_cpu_frequency = get_cpu_frequency();

void rdtsc(uint64_t &x)
{
#ifdef IA32
    uint32_t lval, hval;
    // asm volatile ("push %eax; push %ebx; push %ecx; push %edx");
    // asm volatile ("xor %eax, %eax; cpuid");
    asm volatile("rdtsc" : "=a"(lval), "=d"(hval));
    // asm volatile ("pop %edx; pop %ecx; pop %ebx; pop %eax");
    x = hval;
    x = (x << 32) | lval;
#elif defined(IA64)
    asm("mov %0=ar.itc" : "=r"(x)::"memory");
#elif defined(AMD64)
    uint32_t lval, hval;
    asm("rdtsc" : "=a"(lval), "=d"(hval));
    x = hval;
    x = (x << 32) | lval;
#elif defined(TIMING_USE_QPC)
    // This function should not fail, because we checked the QPC
    // when calling to QueryPerformanceFrequency. If it failed,
    // the m_bUseMicroSecond was set to true.
    QueryPerformanceCounter((LARGE_INTEGER *)&x);
#elif defined(TIMING_USE_MACH_ABS_TIME)
    x = mach_absolute_time();
#else
    // use system call to read time clock for other archs
    timeval t;
    gettimeofday(&t, 0);
    x = t.tv_sec * uint64_t(1000000) + t.tv_usec;
#endif
}

uint64_t get_timestamp_us()
{
#if defined(TIMING_USE_QPC)

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart / s_cpu_frequency;

#elif defined(TIMING_USE_MACH_ABS_TIME)

    const uint64_t x = mach_absolute_time();
    return x / s_cpu_frequency;

#elif defined(TIMING_USE_MONOTONIC_CLOCK)

    // CLOCK_MONOTONIC
    //    Clock that cannot be set and represents monotonic time since
    //    some unspecified starting point.This clock is not affected
    //    by discontinuous jumps in the system time(e.g., if the system
    //    administrator manually changes the clock), but is affected by
    //    the incremental adjustments performed by adjtime(3) and NTP.

    // CLOCK_MONOTONIC_COARSE(since Linux 2.6.32; Linux - specific)
    //    A faster but less precise version of CLOCK_MONOTONIC.Use
    //    when you need very fast, but not fine - grained timestamps.
    //    Requires per - architecture support, and probably also architec‐
    //    ture support for this flag in the vdso(7).

    // СLOCK_MONOTONIC_RAW(since Linux 2.6.28; Linux - specific)
    //    Similar to CLOCK_MONOTONIC, but provides access to a raw hard‐
    //    ware - based time that is not subject to NTP adjustments or the
    //    incremental adjustments performed by adjtime(3).

    struct timespec time;
    // Note: the clock_gettime is defined in librt
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * uint64_t(1000000) + time.tv_nsec / 1000;

#else

    // Note: The time returned by gettimeofday() is affected by discontinuous jumps
    // in the system time (e.g., if the system administrator manually changes the system time).
    // But if we get here, there seem to be no alternatives we can use instead.
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * uint64_t(1000000) + t.tv_usec;

#endif
}

} // namespace sync
} // namespace srt


template<>
uint64_t srt::sync::TimePoint<srt::sync::steady_clock>::us_since_epoch() const
{
    return m_timestamp / s_cpu_frequency;
}


template<>
srt::sync::Duration< srt::sync::steady_clock> srt::sync::TimePoint<srt::sync::steady_clock>::time_since_epoch() const
{
    return srt::sync::Duration< srt::sync::steady_clock>(m_timestamp);
}

srt::sync::TimePoint<srt::sync::steady_clock> srt::sync::steady_clock::now()
{
    uint64_t x = 0;
    rdtsc(x);
    return time_point(x);
}

long long srt::sync::to_microseconds(const steady_clock::duration &t)
{
    return t.count() / s_cpu_frequency;
}


long long srt::sync::to_milliseconds(const steady_clock::duration& t)
{
    return t.count() / s_cpu_frequency / 1000;
}

long long srt::sync::to_seconds(const steady_clock::duration& t)
{
    return t.count() / s_cpu_frequency / 1000000;
}

srt::sync::steady_clock::duration srt::sync::from_microseconds(long t_us)
{
    return steady_clock::duration(t_us * s_cpu_frequency);
}

srt::sync::steady_clock::duration srt::sync::from_milliseconds(long t_ms)
{
    return steady_clock::duration((1000 * t_ms) * s_cpu_frequency);
}

srt::sync::steady_clock::duration srt::sync::from_seconds(long t_s)
{
    return steady_clock::duration((1000000 * t_s) * s_cpu_frequency);
}

#endif

#ifdef USE_STL_CHRONO


//
// SyncCond
//


srt::sync::SyncCond::SyncCond() {}

srt::sync::SyncCond::~SyncCond() {}


bool srt::sync::SyncCond::wait_for(UniqueLock& lk, steady_clock::duration timeout)
{
    return m_tick_cond.wait_for(lk, timeout) != cv_status::timeout;

    // wait_until(steady_clock::now() + timeout);
}


void srt::sync::SyncCond::wait(UniqueLock& lk)
{
    return m_tick_cond.wait(lk);
}


void srt::sync::SyncCond::notify_one() { m_tick_cond.notify_one(); }

void srt::sync::SyncCond::notify_all() { m_tick_cond.notify_all(); }



//
// SyncEvent
//


srt::sync::SyncEvent::SyncEvent() {}

srt::sync::SyncEvent::~SyncEvent() {}

bool srt::sync::SyncEvent::wait_until(time_point<steady_clock> tp)
{
    // TODO: Add busy waiting

    // using namespace srt_logging;
    // LOGC(dlog.Note, log << "SyncEvent::wait_until delta="
    //    << std::chrono::duration_cast<std::chrono::microseconds>(tp - steady_clock::now()).count() << " us");
    std::unique_lock<std::mutex> lk(m_tick_lock);
    m_sched_time = tp;
    return m_tick_cond.wait_until(lk, tp, [this]() { return m_sched_time <= steady_clock::now(); });
}

bool srt::sync::SyncEvent::wait_for(steady_clock::duration timeout)
{
    std::unique_lock<std::mutex> lk(m_tick_lock);
    return m_tick_cond.wait_for(lk, timeout) != cv_status::timeout;

    // wait_until(steady_clock::now() + timeout);
}

bool srt::sync::SyncEvent::wait_for(UniqueLock &lk, steady_clock::duration timeout)
{
    return m_tick_cond.wait_for(lk, timeout) != cv_status::timeout;

    // wait_until(steady_clock::now() + timeout);
}


void srt::sync::SyncEvent::wait(UniqueLock& lk)
{
    return m_tick_cond.wait(lk);
}

void srt::sync::SyncEvent::wait()
{
    std::unique_lock<std::mutex> lk(m_tick_lock);
    return m_tick_cond.wait(lk);
}

void srt::sync::SyncEvent::interrupt()
{
    {
        ScopedLock lock(m_tick_lock);
        m_sched_time = steady_clock::now();
    }

    notify_one();
}

void srt::sync::SyncEvent::notify_one() { m_tick_cond.notify_one(); }

void srt::sync::SyncEvent::notify_all() { m_tick_cond.notify_all(); }

#else


std::string srt::sync::FormatTime(const steady_clock::time_point &time)
{
    const int64_t delta_us = to_microseconds(time - steady_clock::now());
    cerr << "delta_us " << delta_us << endl;

    timeval now;
    gettimeofday(&now, NULL);
    const uint64_t time_us = now.tv_sec * uint64_t(1000000) + now.tv_usec + delta_us;

    time_t tt = time_us / 1000000;
    struct tm tm = SysLocalTime(tt);

    char tmp_buf[512];
    strftime(tmp_buf, 512, "%X.", &tm);

    ostringstream out;
    out << tmp_buf << setfill('0') << setw(6) << (time_us % 1000000);
    return out.str();
}


srt::sync::Mutex::Mutex()
{
    pthread_mutex_init(&m_mutex, NULL);
}


srt::sync::Mutex::~Mutex()
{
    pthread_mutex_destroy(&m_mutex);
}


int srt::sync::Mutex::lock()
{
    return pthread_mutex_lock(&m_mutex);
}


int srt::sync::Mutex::unlock()
{
    return pthread_mutex_unlock(&m_mutex);
}


bool srt::sync::Mutex::try_lock()
{
    return (pthread_mutex_lock(&m_mutex) == 0);
}


srt::sync::ScopedLock::ScopedLock(Mutex& m)
    : m_mutex(m)
{
    m_mutex.lock();
}


srt::sync::ScopedLock::~ScopedLock()
{
    m_mutex.unlock();
}


//
//
//

srt::sync::UniqueLock::UniqueLock(Mutex& m)
    : m_Mutex(m)
{
    m_iLocked = m_Mutex.lock();
}


srt::sync::UniqueLock::~UniqueLock()
{
    unlock();
}


void srt::sync::UniqueLock::unlock()
{
    if (m_iLocked == 0)
    {
        m_Mutex.unlock();
        m_iLocked = -1;
    }
}



srt::sync::SyncEvent::SyncEvent()
    : m_tick_cond()
{
    //m_tick_cond = PTHREAD_COND_INITIALIZER;
    const int res = pthread_cond_init(&m_tick_cond, NULL);
    if (res != 0)
        throw std::runtime_error("pthread_cond_init failed");
}


srt::sync::SyncEvent::~SyncEvent()
{
    pthread_cond_destroy(&m_tick_cond);
}


bool srt::sync::SyncEvent::wait_until(TimePoint<steady_clock> tp)
{
    UniqueLock lck(m_tick_lock);
    // Use class member such that the method can be interrupted by others
    m_sched_time = tp;

    TimePoint<steady_clock> cur_tp = steady_clock::now();

    while (cur_tp < m_sched_time)
    {
#if USE_BUSY_WAITING
#ifdef IA32
        __asm__ volatile("pause; rep; nop; nop; nop; nop; nop;");
#elif IA64
        __asm__ volatile("nop 0; nop 0; nop 0; nop 0; nop 0;");
#elif AMD64
        __asm__ volatile("nop; nop; nop; nop; nop;");
#endif
#else
        const uint64_t wait_us = to_microseconds(m_sched_time - cur_tp);
        // The while loop ensures that (cur_tp < m_sched_time).
        // Conversion to microseconds may lose precision, therefore check for 0.
        if (wait_us == 0)
            return true;

        timeval now;
        gettimeofday(&now, 0);
        const uint64_t time_us = now.tv_sec * uint64_t(1000000) + now.tv_usec + wait_us;
        timespec       timeout;
        timeout.tv_sec  = time_us / 1000000;
        timeout.tv_nsec = (time_us % 1000000) * 1000;

        pthread_cond_timedwait(&m_tick_cond, &lck.m_Mutex.m_mutex, &timeout);
#endif

        cur_tp = steady_clock::now();
    }

    return cur_tp >= m_sched_time;
}

void srt::sync::SyncEvent::notify_one()
{
    pthread_cond_signal(&m_tick_cond);
}

void srt::sync::SyncEvent::notify_all()
{
    pthread_cond_broadcast(&m_tick_cond);
}


void srt::sync::SyncEvent::interrupt()
{
    m_sched_time = steady_clock::now();
    pthread_cond_broadcast(&m_tick_cond);
}

bool srt::sync::SyncEvent::wait_for(const Duration<steady_clock>& rel_time)
{
    UniqueLock lock(m_tick_lock);
    return wait_for(lock, rel_time);
}

bool srt::sync::SyncEvent::wait_for(UniqueLock& lock, const Duration<steady_clock>& rel_time)
{
    timeval now;
    gettimeofday(&now, 0);
    const uint64_t time_us = now.tv_sec * uint64_t(1000000) + now.tv_usec + to_microseconds(rel_time);
    timespec targettime;
    targettime.tv_sec = time_us / 1000000;
    targettime.tv_nsec = (time_us % 1000000) * 1000;

    return (pthread_cond_timedwait(&m_tick_cond, &lock.m_Mutex.m_mutex, &targettime) == 0);
}

void srt::sync::SyncEvent::wait()
{
    UniqueLock lock(m_tick_lock);
    wait(lock);
}

void srt::sync::SyncEvent::wait(UniqueLock& lk)
{
    pthread_cond_wait(&m_tick_cond, &lk.m_Mutex.m_mutex);
}




#endif

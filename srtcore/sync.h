/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#pragma once

//#define USE_STL_CHRONO
//#define ENABLE_CXX17



#include <cstdlib>
#include <pthread.h>
#ifdef USE_STL_CHRONO
#include <chrono>
#include <condition_variable>
#include <mutex>
#else
//#ifndef _WIN32
//#include <sys/time.h>
//#include <sys/uio.h>
//#else
// // #include <winsock2.h>
// //#include <windows.h>
//#endif
//#include <pthread.h>
//#include "udt.h"
#endif

#include "utilities.h"



namespace srt
{
namespace sync
{
using namespace std;

#ifdef USE_STL_CHRONO

template <class Clock, class Duration = typename Clock::duration>
using time_point = chrono::time_point<Clock, Duration>;

using system_clock   = chrono::system_clock;
using high_res_clock = chrono::high_resolution_clock;
using steady_clock   = chrono::steady_clock;

uint64_t get_timestamp_us();

inline long long to_microseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
}

inline long long to_microseconds(const steady_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

inline long long to_milliseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

inline steady_clock::duration from_microseconds(long t_us) { return std::chrono::microseconds(t_us); }

inline steady_clock::duration from_milliseconds(long t_ms) { return std::chrono::milliseconds(t_ms); }

inline steady_clock::duration from_seconds(long t_s) { return std::chrono::seconds(t_s); }

template <class Clock, class Duration = typename Clock::duration>
inline bool is_zero(const time_point<Clock, Duration> &tp)
{
    return tp.time_since_epoch() == Clock::duration::zero();
}

#else

template <class _Clock>
class Duration
{

public:
    Duration()
        : m_duration(0)
    {
    }

    explicit Duration(int64_t d)
        : m_duration(d)
    {
    }

public:
    inline int64_t count() const { return m_duration; }

    static Duration zero() { return Duration(); }

public: // Logical operators
    inline bool operator>=(const Duration &rhs) const { return m_duration >= rhs.m_duration; }
    inline bool operator>(const Duration &rhs) const { return m_duration > rhs.m_duration; }
    inline bool operator==(const Duration &rhs) const { return m_duration == rhs.m_duration; }
    inline bool operator!=(const Duration& rhs) const { return m_duration != rhs.m_duration; }
    inline bool operator<=(const Duration &rhs) const { return m_duration <= rhs.m_duration; }
    inline bool operator<(const Duration &rhs) const { return m_duration < rhs.m_duration; }

public: // Arythmetic operators
    inline void operator*=(const double mult) { m_duration *= mult; }
    inline void operator+=(const Duration &rhs) { m_duration += rhs.m_duration; }
    inline void operator-=(const Duration &rhs) { m_duration -= rhs.m_duration; }

    inline Duration operator+(const Duration &rhs) const { return Duration(m_duration + rhs.m_duration); }
    inline Duration operator-(const Duration &rhs) const { return Duration(m_duration - rhs.m_duration); }
    inline Duration operator*(const int& rhs) const { return Duration(m_duration * rhs); }

private:
    // int64_t range is from -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
    int64_t m_duration;
};



template <class _Clock> class TimePoint;

class steady_clock
{
    // Mapping to rdtsc

public:
    typedef Duration<steady_clock> duration;
    typedef TimePoint<steady_clock> time_point;

 public:
    static time_point now();
};

template <class _Clock>
class TimePoint
{ // represents a point in time

public:
    TimePoint()
        : m_timestamp(0)
    {
    }

    TimePoint(uint64_t tp)
        : m_timestamp(tp)
    {
    }

public:
    inline bool operator<(const TimePoint<_Clock> &rhs) const { return m_timestamp < rhs.m_timestamp; }
    inline bool operator<=(const TimePoint<_Clock> &rhs) const { return m_timestamp <= rhs.m_timestamp; }
    inline bool operator==(const TimePoint<_Clock> &rhs) const { return m_timestamp == rhs.m_timestamp; }
    inline bool operator>=(const TimePoint<_Clock> &rhs) const { return m_timestamp >= rhs.m_timestamp; }
    inline bool operator>(const TimePoint<_Clock> &rhs) const { return m_timestamp > rhs.m_timestamp; }

    inline Duration<_Clock> operator-(const TimePoint<_Clock> &rhs) const { return Duration<_Clock>(m_timestamp - rhs.m_timestamp); }
    inline TimePoint operator+(const Duration<_Clock> &rhs) const { return TimePoint(m_timestamp + rhs.count()); }
    inline TimePoint operator-(const Duration< _Clock>& rhs) const { return TimePoint(m_timestamp - rhs.count()); }

    inline void operator+=(const Duration< _Clock>& rhs) { m_timestamp += rhs.count(); }

public:

    uint64_t us_since_epoch() const;
    Duration<steady_clock> time_since_epoch() const;

public:

    bool is_zero() const { return m_timestamp == 0; }

private:
    uint64_t m_timestamp;
};


template<>
uint64_t srt::sync::TimePoint<srt::sync::steady_clock>::us_since_epoch() const;

template<>
srt::sync::Duration<srt::sync::steady_clock> srt::sync::TimePoint<srt::sync::steady_clock>::time_since_epoch() const;



inline Duration<steady_clock> operator*(const int& lhs, const Duration<steady_clock>& rhs) { return rhs * lhs; }


inline long long to_microseconds(const TimePoint<steady_clock> tp)
{
    return static_cast<long long>(tp.us_since_epoch());
}

long long to_microseconds(const steady_clock::duration& t);
long long to_milliseconds(const steady_clock::duration& t);
long long to_seconds(const steady_clock::duration& t);

Duration<steady_clock> from_microseconds(long t_us);
Duration<steady_clock> from_milliseconds(long t_ms);
Duration<steady_clock> from_seconds(long t_s);

inline bool is_zero(const TimePoint<steady_clock>& t) { return t.is_zero(); }

#endif  // USE_STL_CHRONO

// Mutex section
#ifdef USE_STL_CHRONO
// Mutex for C++03 should call pthread init and destroy
using Mutex      = mutex;
using UniqueLock = unique_lock<mutex>;
#if ENABLE_CXX17
using ScopedLock = scoped_lock<mutex>;
#else
using ScopedLock = lock_guard<mutex>;
#endif

using Thread     = thread;

inline void SleepFor(const steady_clock::duration &t) { this_thread::sleep_for(t); }


#else


class Mutex
{
    friend class SyncEvent;

public:

    Mutex();
    ~Mutex();

public:

    int lock();
    int unlock();

    /// @return     true if the lock was acquired successfully, otherwise false
    bool try_lock();

private:

    pthread_mutex_t m_mutex;
};



class ScopedLock
{
public:

    ScopedLock(Mutex& m);
    ~ScopedLock();

private:


    Mutex& m_mutex;
};



class UniqueLock
{
    friend class SyncEvent;
public:


    UniqueLock(Mutex &m);


    ~UniqueLock();


public:

    void unlock();

private:

    int m_iLocked;
    Mutex& m_Mutex;
};


#endif  // USE_STL_CHRONO


struct CriticalSection
{
    static void enter(Mutex &m) { m.lock(); }
    static void leave(Mutex &m) { m.unlock(); }
};


class InvertedLock
{
    Mutex *m_pMutex;

  public:
    InvertedLock(Mutex *m)
        : m_pMutex(m)
    {
        if (!m_pMutex)
            return;

        CriticalSection::leave(*m_pMutex);
    }

    ~InvertedLock()
    {
        if (!m_pMutex)
            return;
        CriticalSection::enter(*m_pMutex);
    }
};


inline void SleepFor(const Duration<steady_clock>& t)
{
#ifndef _WIN32
    usleep(to_microseconds(t));
#else
    Sleep((DWORD) to_milliseconds(t));
#endif
}



class SyncCond
{

public:
    SyncCond();

    ~SyncCond();


public:


    bool wait_for(UniqueLock& lk, Duration<steady_clock> timeout);

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

private:
#ifdef USE_STL_CHRONO
    condition_variable m_tick_cond;
#else
    pthread_cond_t  m_tick_cond;
#endif
};



class SyncEvent
{

  public:
    SyncEvent();

    ~SyncEvent();

  public:

    Mutex &mutex() { return m_tick_lock; }

  public:
    /// @return true  if condition occured
    ///         false on timeout
    bool wait_until(TimePoint<steady_clock> tp);

    /// Can have spurious wake ups
    /// @return true  if condition occured
    ///         false on timeout
    bool wait_for(Duration<steady_clock> timeout);

    bool wait_for(UniqueLock &lk, Duration<steady_clock> timeout);

    void wait();

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

    void interrupt();

  private:
#ifdef USE_STL_CHRONO
    Mutex              m_tick_lock;
    condition_variable m_tick_cond;
#else
    Mutex              m_tick_lock;
    pthread_cond_t     m_tick_cond;
#endif
    TimePoint<steady_clock> m_sched_time;
};

static SyncEvent s_SyncEvent;



std::string FormatTime(const TimePoint<steady_clock>& time);

}; // namespace sync
}; // namespace srt

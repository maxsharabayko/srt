/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__SRT_UTILITIES_H
#define INC__SRT_UTILITIES_H


#ifdef __GNUG__
#define ATR_UNUSED __attribute__((unused))
#define ATR_DEPRECATED __attribute__((deprecated))
#else
#define ATR_UNUSED
#define ATR_DEPRECATED
#endif

#if defined(__cplusplus) && __cplusplus > 199711L
#define HAVE_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
// Microsoft Visual Studio supports C++11, but not fully,
// and still did not change the value of __cplusplus. Treat
// this special way.
// _MSC_VER == 1800  means Microsoft Visual Studio 2013.
#elif defined(_MSC_VER) && _MSC_VER >= 1800
#define HAVE_CXX11 1
#define ATR_NOEXCEPT
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#else
#define HAVE_CXX11 0
#define ATR_NOEXCEPT // throw() - bad idea
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL

#if defined(REQUIRE_CXX11) && REQUIRE_CXX11 == 1
#error "The currently compiled application required C++11, but your compiler doesn't support it."
#endif

#endif

// Windows warning disabler
#define _CRT_SECURE_NO_WARNINGS 1

#include "platform_sys.h"

// Happens that these are defined, undefine them in advance
#undef min
#undef max

#include <string>
#include <algorithm>
#include <bitset>
#include <map>
#include <functional>
#include <memory>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstring>

// -------------- UTILITIES ------------------------

// Bit numbering utility.
//
// This is something that allows you to turn 32-bit integers into bit fields.
// Although bitfields are part of C++ language, they are not designed to be
// interchanged with 32-bit numbers, and any attempt to doing it (by placing
// inside a union, for example) is nonportable (order of bitfields inside
// same-covering 32-bit integer number is dependent on the endian), so they are
// popularly disregarded as useless. Instead the 32-bit numbers with bits
// individually selected is preferred, with usually manual playing around with
// & and | operators, as well as << and >>. This tool is designed to simplify
// the use of them. This can be used to qualify a range of bits inside a 32-bit
// number to be a separate number, you can "wrap" it by placing the integer
// value in the range of these bits, as well as "unwrap" (extract) it from
// the given place. For your own safety, use one prefix to all constants that
// concern bit ranges intended to be inside the same "bit container".
//
// Usage: typedef Bits<leftmost, rightmost> MASKTYPE;  // MASKTYPE is a name of your choice.
//
// With this defined, you can use the following members:
// - MASKTYPE::mask - to get the int32_t value with bimask (used bits set to 1, others to 0)
// - MASKTYPE::offset - to get the lowermost bit number, or number of bits to shift
// - MASKTYPE::wrap(int value) - to create a bitset where given value is encoded in given bits
// - MASKTYPE::unwrap(int bitset) - to extract an integer value from the bitset basing on mask definition
// (rightmost defaults to leftmost)
// REMEMBER: leftmost > rightmost because bit 0 is the LEAST significant one!

template <size_t L, size_t R, bool parent_correct = true>
struct BitsetMask
{
    static const bool correct = L >= R;
    static const uint32_t value = (1u << L) | BitsetMask<L-1, R, correct>::value;
};

// This is kind-of functional programming. This describes a special case that is
// a "terminal case" in case when decreased L-1 (see above) reached == R.
template<size_t R>
struct BitsetMask<R, R, true>
{
    static const bool correct = true;
    static const uint32_t value = 1 << R;
};

// This is a trap for a case that BitsetMask::correct in the master template definition
// evaluates to false. This trap causes compile error and prevents from continuing
// recursive unwinding in wrong direction (and challenging the compiler's resistiveness
// for infinite loops).
template <size_t L, size_t R>
struct BitsetMask<L, R, false>
{
};

template <size_t L, size_t R = L>
struct Bits
{
    // DID YOU GET kind-of error: ‘mask’ is not a member of ‘Bits<3u, 5u, false>’ ?
    // See the the above declaration of 'correct' !
    static const uint32_t mask = BitsetMask<L, R>::value;
    static const uint32_t offset = R;
    static const size_t size = L - R + 1;

    // Example: if our bitset mask is 00111100, this checks if given value fits in
    // 00001111 mask (that is, does not exceed <0, 15>.
    static bool fit(uint32_t value) { return (BitsetMask<L-R, 0>::value & value) == value; }

    /// 'wrap' gets some given value that should be placed in appropriate bit range and
    /// returns a whole 32-bit word that has the value already at specified place.
    /// To create a 32-bit container that contains already all values destined for different
    /// bit ranges, simply use wrap() for each of them and bind them with | operator.
    static uint32_t wrap(uint32_t baseval) { return (baseval << offset) & mask; }

    /// Extracts appropriate bit range and returns them as normal integer value.
    static uint32_t unwrap(uint32_t bitset) { return (bitset & mask) >> offset; }

    template<class T>
    static T unwrapt(uint32_t bitset) { return static_cast<T>(unwrap(bitset)); }
};


//inline int32_t Bit(size_t b) { return 1 << b; }
// XXX This would work only with 'constexpr', but this is
// available only in C++11. In C++03 this can be only done
// using a macro.
//
// Actually this can be expressed in C++11 using a better technique,
// such as user-defined literals:
// 2_bit  --> 1 >> 2

#ifdef BIT
#undef BIT
#endif
#define BIT(x) (1 << (x))


// ------------------------------------------------------------
// This is something that reminds a structure consisting of fields
// of the same type, implemented as an array. It's parametrized
// by the type of fields and the type, which's values should be
// used for indexing (preferably an enum type). Whatever type is
// used for indexing, it is converted to size_t for indexing the
// actual array.
// 
// The user should use it as an array: ds[DS_NAME], stating
// that DS_NAME is of enum type passed as 3rd parameter.
// However trying to do ds[0] would cause a compile error.
template <typename FieldType, size_t NoOfFields, typename IndexerType>
struct DynamicStruct
{
    FieldType inarray[NoOfFields];

    void clear()
    {
        // As a standard library, it can be believed that this call
        // can be optimized when FieldType is some integer.
        std::fill(inarray, inarray + NoOfFields, FieldType());
    }

    FieldType operator[](IndexerType ix) const { return inarray[size_t(ix)]; }
    FieldType& operator[](IndexerType ix) { return inarray[size_t(ix)]; }

    template<class AnyOther>
    FieldType operator[](AnyOther ix) const
    {
        // If you can see a compile error here ('int' is not a class or struct, or
        // that there's no definition of 'type' in given type), it means that you
        // have used invalid data type passed to [] operator. See the definition
        // of this type as DynamicStruct and see which type is required for indexing.
        typename AnyOther::type wrong_usage_of_operator_index = AnyOther::type;
        return inarray[size_t(ix)];
    }

    template<class AnyOther>
    FieldType& operator[](AnyOther ix)
    {
        // If you can see a compile error here ('int' is not a class or struct, or
        // that there's no definition of 'type' in given type), it means that you
        // have used invalid data type passed to [] operator. See the definition
        // of this type as DynamicStruct and see which type is required for indexing.
        typename AnyOther::type wrong_usage_of_operator_index = AnyOther::type;
        return inarray[size_t(ix)];
    }

    operator FieldType* () { return inarray; }
    operator const FieldType* () const { return inarray; }

    char* raw() { return (char*)inarray; }
};


// ------------------------------------------------------------



inline bool IsSet(int32_t bitset, int32_t flagset)
{
    return (bitset & flagset) == flagset;
}

inline void HtoNLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = htonl(src[i]);
}

inline void NtoHLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = ntohl(src[i]);
}

// Homecooked version of ref_t. It's a copy of std::reference_wrapper
// voided of unwanted properties and renamed to ref_t.


#if HAVE_CXX11
#include <functional>
#endif

template<typename Type>
class ref_t
{
    Type* m_data;

public:
    typedef Type type;

#if HAVE_CXX11
    explicit ref_t(Type& __indata)
        : m_data(std::addressof(__indata))
        { }
#else
    explicit ref_t(Type& __indata)
        : m_data((Type*)(&(char&)(__indata)))
        { }
#endif

    ref_t(const ref_t<Type>& inref)
        : m_data(inref.m_data)
    { }

#if HAVE_CXX11
    ref_t(const std::reference_wrapper<Type>& i): m_data(std::addressof(i.get())) {}
#endif

    Type& operator*() { return *m_data; }

    Type& get() const
    { return *m_data; }

    Type operator->() const
    { return *m_data; }
};


#if HAVE_CXX11

// This alias was created so that 'Ref' (not 'ref') is used everywhere.
// Normally the C++11 'ref' fits perfectly here, however in C++03 mode
// it would have to be newly created. This would then cause a conflict
// between C++03 SRT and C++11 applications as well as between C++ standard
// library and SRT when SRT is compiled in C++11 mode (as it happens on
// Darwin/clang).
template <class In>
inline auto Ref(In& i) -> decltype(std::ref(i)) { return std::ref(i); }

template <class In>
inline auto Move(In& i) -> decltype(std::move(i)) { return std::move(i); }

// Gluing string of any type, wrapper for operator <<

template <class Stream>
inline Stream& Print(Stream& in) { return in;}

template <class Stream, class Arg1, class... Args>
inline Stream& Print(Stream& sout, Arg1&& arg1, Args&&... args)
{
    sout << arg1;
    return Print(sout, args...);
}

template <class... Args>
inline std::string Sprint(Args&&... args)
{
    std::ostringstream sout;
    Print(sout, args...);
    return sout.str();
}

// We need to use UniquePtr, in the form of C++03 it will be a #define.
// Naturally will be used std::move() so that it can later painlessly
// switch to C++11.
template <class T>
using UniquePtr = std::unique_ptr<T>;

// Some utilities borrowed from tumux, as this is using options
// similar way.
template <class Container, class Value = typename Container::value_type, typename... Args> inline
std::string Printable(const Container& in, Value /*pseudoargument*/, Args&&... args)
{
    std::ostringstream os;
    Print(os, args...);
    os << "[ ";
    for (auto i: in)
        os << Value(i) << " ";
    os << "]";
    return os.str();
}

template <class Container> inline
std::string Printable(const Container& in)
{
    using Value = typename Container::value_type;
    return Printable(in, Value());
}

template<typename Map, typename Key>
auto map_get(Map& m, const Key& key, typename Map::mapped_type def = typename Map::mapped_type()) -> typename Map::mapped_type
{
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

template<typename Map, typename Key>
auto map_getp(Map& m, const Key& key) -> typename Map::mapped_type*
{
    auto it = m.find(key);
    return it == m.end() ? nullptr : std::addressof(it->second);
}


#else

template <class Type>
ref_t<Type> Ref(Type& arg)
{
    return ref_t<Type>(arg);
}

// The unique_ptr requires C++11, and the rvalue-reference feature,
// so here we're simulate the behavior using the old std::auto_ptr.

// This is only to make a "move" call transparent and look ok towards
// the C++11 code.
template <class T>
std::auto_ptr_ref<T> Move(const std::auto_ptr_ref<T>& in) { return in; }

// We need to provide also some fixes for this type that were not present in auto_ptr,
// but they are present in unique_ptr.

// C++03 doesn't have a templated typedef, but still we need some things
// that can only function as a class.
template <class T>
class UniquePtr: public std::auto_ptr<T>
{
    typedef std::auto_ptr<T> Base;

public:

    // This is a template - so method names must be declared explicitly
    typedef typename Base::element_type element_type;
    using Base::get;
    using Base::reset;

    // All constructor declarations must be repeated.
    // "Constructor delegation" is also only C++11 feature.
    explicit UniquePtr(element_type* __p = 0) throw() : Base(__p) {}
    UniquePtr(UniquePtr& __a) throw() : Base(__a) { }
    template<typename _Tp1>
    UniquePtr(UniquePtr<_Tp1>& __a) throw() : Base(__a) {}

    UniquePtr& operator=(UniquePtr& __a) throw() { return Base::operator=(__a); }
    template<typename _Tp1>
    UniquePtr& operator=(UniquePtr<_Tp1>& __a) throw() { return Base::operator=(__a); }

    // Good, now we need to add some parts of the API of unique_ptr.

    bool operator==(const UniquePtr& two) const { return get() == two.get(); }
    bool operator!=(const UniquePtr& two) const { return get() != two.get(); }

    bool operator==(const element_type* two) const { return get() == two; }
    bool operator!=(const element_type* two) const { return get() != two; }

    operator bool () { return 0!= get(); }
};


#endif


// This is a stack, onto which you push values, but
// you never pop them. They stay there forever, but 
// the oldest get dropped. You can backtrack up to
// the last N items backward using the index:
// - 0: the top of the stack
// - 1, 2... the N'th older in the push history.
// - up to SPAN value
template <typename TYPE, unsigned SPAN>
class RingLossyStack
{
    TYPE m_qValueStack[SPAN];

    size_t m_zStackPos;
    size_t m_zStackSize;

    template <unsigned N, bool CORRECT /*true*/>
    TYPE history_tracker_hlp()
    {
        return m_qValueStack[(SPAN + m_zStackPos - N) % SPAN];
    }

public:

    typedef TYPE value_type;

    static const unsigned span = SPAN;

    struct iterator
    {
        TYPE* array;
        size_t index;

        // IMPORTANT !!!
        // The ++ operator goes backward in the internal array.
        // The [0] position is the top, then towards SPAN value
        // it goes back into the array.
        iterator& operator++()
        {
            if (index == 0)
                index = SPAN - 1;
            else
                --index;
            return *this;
        }

        iterator operator++(int)
        {
            iterator i = *this;
            ++i;
            return i;
        }

        TYPE& operator*()
        {
            return array[index];
        }

        bool operator==(const iterator & y)
        {
            return index == y.index;
        }

        bool operator!=(const iterator & y)
        {
            return index != y.index;
        }
    };

#if ENABLE_LOGGING

    std::string dump()
    {
        std::ostringstream os;
        os << "pos=" << m_zStackPos << " size=" << m_zStackSize << " SPAN=" << SPAN
            << " DATA: ";

        for (size_t i = 0; i < SPAN; ++i)
            os << m_qValueStack[i] << " ";

        return os.str();
    }
#endif

    typedef typename RingLossyStack<const TYPE, SPAN>::iterator const_iterator;

    size_t beginpos() const
    {
        if (m_zStackSize == 0)
            return 0; // will be equal to end
        if (m_zStackPos == 0)
            return SPAN - 1;
        return m_zStackPos - 1;
    }

    size_t endpos() const
    {
        // Until the full size is reached, it behaves
        // as a normal array. Return the highest position.
        if (m_zStackSize != SPAN)
            return m_zStackSize;

        // m_zStackPos is the position to be filled
        return m_zStackPos;
    }


    iterator begin()
    {
        iterator i = { m_qValueStack, beginpos() };
        return i;
    }

    const_iterator begin() const
    {
        const_iterator i = { m_qValueStack, beginpos() };
        return i;
    }

    // m_zStackPos points to the current position 
    iterator end()
    {
        iterator i = { m_qValueStack, endpos() };
        return i;
    }

    const_iterator end() const
    {
        const_iterator i = { m_qValueStack, endpos() };
        return i;
    }

    RingLossyStack() : m_zStackPos(), m_zStackSize() {}

    void push(const TYPE & val)
    {
        m_qValueStack[m_zStackPos] = val;
        ++m_zStackPos;
        if (m_zStackPos == SPAN)
            m_zStackPos = 0;
        if (m_zStackSize < SPAN)
            ++m_zStackSize;
    }

    /* This doesn't make sense in case of a ring buffer
    template <unsigned N>
    TYPE history()
    {
        return IndexArray<TYPE, N, (N < SPAN) >::perform(m_qValueStack);
    }
    */

    size_t size() { return m_zStackSize; }
    TYPE top() { return m_qValueStack[m_zStackPos]; }
    bool empty() { return m_zStackSize == 0; }
    bool full() { return m_zStackSize == SPAN; }

    TYPE operator[](size_t index)
    {
        return m_qValueStack[(SPAN + m_zStackPos - index) % SPAN];
    }

    TYPE average()
    {
        if (m_zStackSize == SPAN)
            return accumulate_array(m_qValueStack) / SPAN;

        // Without this check this below may end up with div/0.
        if (m_zStackSize == 0)
            return TYPE();

        size_t firstsize = SPAN - m_zStackPos;
        if (firstsize > m_zStackSize) // all used data are in the second segment
            return std::accumulate(m_qValueStack + m_zStackPos, m_qValueStack + m_zStackPos + m_zStackSize, TYPE()) / m_zStackSize;

        size_t remainsize = m_zStackSize - firstsize;
        return std::accumulate(m_qValueStack + m_zStackPos, m_qValueStack + SPAN,
            // as the initial value, we use the sum from the first segment
            std::accumulate(m_qValueStack, m_qValueStack + remainsize, TYPE())) / m_zStackSize;
    }
};


template <typename TYPE, unsigned N_SEGMENTS, unsigned SEGMENT_SPAN>
class JumpingAverage
{
    TYPE m_qSegments[N_SEGMENTS]; // variables that keep the fragmentary average
    TYPE m_qValueStack[SEGMENT_SPAN]; // values collected during updating

    size_t m_zRingPos; // First position in the ring-buffer segment array
    size_t m_zRingSize; // How many of the ring items are in use (grows up to N_SEGMENTS)
    size_t m_zStackPos; // Position to be filled by the next update

    TYPE m_qStableAverage;

public:

    JumpingAverage() :
        m_zRingPos(),
        m_zRingSize(),
        m_zStackPos(),
        m_qStableAverage()
    {
    }

    void update(TYPE value)
    {
        // Try to add the value to the position in the value stack first.
        m_qValueStack[m_zStackPos] = value;
        ++m_zStackPos;

        if (m_zStackPos == SEGMENT_SPAN)
        {
            // Good, time to sum up everything and dump to the segments.
            TYPE average = accumulate_array(m_qValueStack) / SEGMENT_SPAN;
            m_zStackPos = 0; // start here

            // Now put it under the ring position.
            m_qSegments[m_zRingPos] = average;

            // Now move the ring. Mind the ring.
            ++m_zRingPos;
            if (m_zRingPos == N_SEGMENTS)
                m_zRingPos = 0;

            if (m_zRingSize < N_SEGMENTS)
            {
                // Extend the size, if not reached yet already
                ++m_zRingSize;
                // As a new segment has been added, calculate now
                // the jumping average value out of all segments
                // and save it.
                m_qStableAverage = std::accumulate(m_qSegments, m_qSegments + m_zRingSize, 0) / m_zRingSize;
            }
            else
            {
                // Otherwise the ring size stays with the maximum value,
                // which means that it will use N segments backwards.
                m_qStableAverage = accumulate_array(m_qSegments) / N_SEGMENTS;
            }
        }
    }

    TYPE stableAverage()
    {
        return m_qStableAverage;
    }

    size_t size()
    {
        // This value represents the number of all values collected so far.
        return m_zRingPos * SEGMENT_SPAN + m_zStackPos;
    }

    // Needs external mutex in case of multithreading
    TYPE currentAverage()
    {
        TYPE stav = m_qStableAverage;
        if (m_zStackPos > 0)
        {
            TYPE adstav = std::accumulate(m_qValueStack, m_qValueStack + m_zStackPos, 0) / m_zStackPos;
            stav = (stav + adstav) / 2;
        }
        return stav;
    }
};



template<unsigned SEGMENT_SPAN_I, unsigned SEGMENT_NUMBER_I, int MAX_DRIFT_I>
class FastDriftTracer
{
    int64_t m_qDriftSumSeg[SEGMENT_NUMBER_I];

    unsigned m_uDriftSpan; // how many elements are filled.

    int64_t m_qDrift;
    int64_t m_qOverdrift;

    static void size_assertion()
    {
        // SEGMENT_NUMBER_I must be at least 2.
        // Segment number 10 is extraordinary, so this is
        // tested as well (as possibly falsely overridden unsigned)
        static const int X = 1 / (SEGMENT_NUMBER_I < 2 || SEGMENT_NUMBER_I > 10 ? 0 : 1);
        (void)X;
    }

public:

    static const unsigned MAX_SPAN = SEGMENT_SPAN_I * SEGMENT_NUMBER_I;
    static const int MAX_DRIFT = MAX_DRIFT_I;

    FastDriftTracer() :
        m_qDriftSumSeg(),
        m_uDriftSpan(0),
        m_qDrift(0),
        m_qOverdrift(0)
    {
        size_assertion();
    }

    void shiftSegments()
    {
        // Copy over. Ranges overlap, but source range is in front
        // of the target range.
        std::copy(m_qDriftSumSeg + 1, m_qDriftSumSeg + SEGMENT_NUMBER_I,
            m_qDriftSumSeg);

        // Clear the newly shifted-in segment
        m_qDriftSumSeg[SEGMENT_NUMBER_I - 1] = 0;

        // Should be same as m_uDriftSpan -= SEGMENT_SPAN_I, but don't be too trusftul.
        m_uDriftSpan = SEGMENT_SPAN_I * (SEGMENT_NUMBER_I - 1);
    }

    bool update(int64_t driftval)
    {
        // We start from value 0, and m_uDriftSpan is under control

        // Calculate which segment should be updated.
        unsigned nseg = m_uDriftSpan / SEGMENT_SPAN_I;
        // Sanity check
        if (nseg >= SEGMENT_NUMBER_I)
        {
            // Maybe report error?
            m_uDriftSpan = 0;
            return false;
        }

        m_qDriftSumSeg[nseg] += driftval;
        ++m_uDriftSpan;
        if (m_uDriftSpan == MAX_SPAN)
        {
            // Only when this size was achieved, should the average be calculated.
            // (This does the same as std::accumulate, but accepts arrays of constant size
            // and does not use a loop - enforces compiler to expand the + expression in place).
            m_qOverdrift = 0;
            m_qDrift = accumulate_array(m_qDriftSumSeg) / MAX_SPAN;
            shiftSegments();

            // In case of "overdrift", save the overdriven value in 'm_qOverdrift'.
            // In clear mode, you should add this value to the time base when update()
            // returns true. The drift value will be since now measured with the
            // overdrift assumed to be added to the base.
            if (std::abs(m_qDrift) > MAX_DRIFT)
            {
                m_qOverdrift = m_qDrift < 0 ? -MAX_DRIFT : MAX_DRIFT;
                m_qDrift -= m_qOverdrift;
            }

            // Inform that the values were just updated.
            return true;
        }

        return false;
    }

    std::string stats()
    {
        std::ostringstream os;
        unsigned nseg = m_uDriftSpan / SEGMENT_SPAN_I;

        os << "DRIFT STATS: SUMS: [ ";

        for (unsigned i = 0; i < SEGMENT_NUMBER_I; ++i)
        {
            unsigned size = i == nseg ? (m_uDriftSpan - (nseg * SEGMENT_SPAN_I)) : SEGMENT_SPAN_I;
            os << m_qDriftSumSeg[i] << "~" << (size ? m_qDriftSumSeg[i] / size : 0) << " ";
        }
        os << "] seg=" << nseg << " span=" << m_uDriftSpan;
        return os.str();
    }

    // These values can be read at any time, however if you want
    // to depend on the fact that they have been changed lately,
    // you have to check the return value from update().
    //
    // IMPORTANT: drift() can be called at any time, just remember
    // that this value may look different than before only if the
    // last update() returned true, which need not be important for you.
    //
    // CASE: CLEAR_ON_UPDATE = true
    // overdrift() should be read only immediately after update() returned
    // true. It will stay available with this value until the next time when
    // update() returns true, in which case the value will be cleared.
    // Therefore, after calling update() if it retuns true, you should read
    // overdrift() immediately an make some use of it. Next valid overdrift
    // will be then relative to every previous overdrift.
    //
    // CASE: CLEAR_ON_UPDATE = false
    // overdrift() will start from 0, but it will always keep track on
    // any changes in overdrift. By manipulating the MAX_DRIFT parameter
    // you can decide how high the drift can go relatively to stay below
    // overdrift.
    int64_t drift() { return m_qDrift; }
    int64_t overdrift() { return m_qOverdrift; }
    unsigned span() { return m_uDriftSpan; }
    static unsigned max() { return MAX_SPAN; }
};


inline std::string FormatBinaryString(const uint8_t* bytes, size_t size)
{
    if ( size == 0 )
        return "";

    //char buf[256];
    using namespace std;

    ostringstream os;

    // I know, it's funny to use sprintf and ostringstream simultaneously,
    // but " %02X" in iostream is: << " " << hex << uppercase << setw(2) << setfill('0') << VALUE << setw(1)
    // Too noisy. OTOH ostringstream solves the problem of memory allocation
    // for a string of unpredictable size.
    //sprintf(buf, "%02X", int(bytes[0]));

    os.fill('0');
    os.width(2);
    os.setf(ios::basefield, ios::hex);
    os.setf(ios::uppercase);

    //os << buf;
    os << int(bytes[0]);


    for (size_t i = 1; i < size; ++i)
    {
        //sprintf(buf, " %02X", int(bytes[i]));
        //os << buf;
        os << int(bytes[i]);
    }
    return os.str();
}


// This function is useful in multiple uses where
// the time drift should be traced. It's currently in use in every
// solution that implements any kind of TSBPD (AKA Stower).
template<unsigned MAX_SPAN, int MAX_DRIFT, bool CLEAR_ON_UPDATE = true>
class DriftTracer
{
    int64_t m_qDrift;
    int64_t m_qOverdrift;

    int64_t m_qDriftSum;
    unsigned m_uDriftSpan;

public:
    DriftTracer()
        : m_qDrift(),
        m_qOverdrift(),
        m_qDriftSum(),
        m_uDriftSpan()
    {}

    bool update(int64_t driftval)
    {
        m_qDriftSum += driftval;
        ++m_uDriftSpan;

        if ( m_uDriftSpan >= MAX_SPAN )
        {
            if ( CLEAR_ON_UPDATE )
                m_qOverdrift = 0;

            // Calculate the median of all drift values.
            // In most cases, the divisor should be == MAX_SPAN.
            m_qDrift = m_qDriftSum / m_uDriftSpan;

            // And clear the collection
            m_qDriftSum = 0;
            m_uDriftSpan = 0;

            // In case of "overdrift", save the overdriven value in 'm_qOverdrift'.
            // In clear mode, you should add this value to the time base when update()
            // returns true. The drift value will be since now measured with the
            // overdrift assumed to be added to the base.
            if (std::abs(m_qDrift) > MAX_DRIFT)
            {
                m_qOverdrift = m_qDrift < 0 ? -MAX_DRIFT : MAX_DRIFT;
                m_qDrift -= m_qOverdrift;
            }

            // printDriftOffset(m_qOverdrift, m_qDrift);

            // Timebase is separate
            // m_qTimeBase += m_qOverdrift;

            return true;
        }
        return false;
    }

    // These values can be read at any time, however if you want
    // to depend on the fact that they have been changed lately,
    // you have to check the return value from update().
    //
    // IMPORTANT: drift() can be called at any time, just remember
    // that this value may look different than before only if the
    // last update() returned true, which need not be important for you.
    //
    // CASE: CLEAR_ON_UPDATE = true
    // overdrift() should be read only immediately after update() returned
    // true. It will stay available with this value until the next time when
    // update() returns true, in which case the value will be cleared.
    // Therefore, after calling update() if it retuns true, you should read
    // overdrift() immediately an make some use of it. Next valid overdrift
    // will be then relative to every previous overdrift.
    //
    // CASE: CLEAR_ON_UPDATE = false
    // overdrift() will start from 0, but it will always keep track on
    // any changes in overdrift. By manipulating the MAX_DRIFT parameter
    // you can decide how high the drift can go relatively to stay below
    // overdrift.
    int64_t drift() { return m_qDrift; }
    int64_t overdrift() { return m_qOverdrift; }
};

template <class KeyType, class ValueType>
struct MapProxy
{
    std::map<KeyType, ValueType>& mp;
    const KeyType& key;

    MapProxy(std::map<KeyType, ValueType>& m, const KeyType& k): mp(m), key(k) {}

    void operator=(const KeyType& val)
    {
        mp[key] = val;
    }

    typename std::map<KeyType, ValueType>::iterator find()
    {
        return mp.find(key);
    }

    typename std::map<KeyType, ValueType>::const_iterator find() const
    {
        return mp.find(key);
    }

    operator ValueType() const
    {
        typename std::map<KeyType, ValueType>::const_iterator p = find();
        if (p == mp.end())
            return "";
        return p->second;
    }

    bool exists() const
    {
        return find() != mp.end();
    }
};


template <class OutputIterator>
inline void Split(const std::string & str, char delimiter, OutputIterator tokens)
{
    if ( str.empty() )
        return; // May cause crash and won't extract anything anyway

    std::size_t start;
    std::size_t end = -1;

    do
    {
        start = end + 1;
        end = str.find(delimiter, start);
        *tokens = str.substr(
                start,
                (end == std::string::npos) ? std::string::npos : end - start);
        ++tokens;
    } while (end != std::string::npos);
}

template <class It>
inline size_t safe_advance(It& it, size_t num, It end)
{
    while ( it != end && num )
    {
        --num;
        ++it;
    }

    return num; // will be effectively 0, if reached the required point, or >0, if end was by that number earlier
}

// This is available only in C++17, dunno why not C++11 as it's pretty useful.
template <class V, size_t N> inline
ATR_CONSTEXPR size_t Size(const V (&)[N]) ATR_NOEXCEPT { return N; }


template <size_t SPAN, typename ValueType>
inline ValueType avg_iir(ValueType old_value, ValueType new_value)
{
    return (old_value * (SPAN - 1) + new_value) / SPAN;
}

// Version where the span value can be given as variable, not compile-time constant.
template<typename ValueType>
inline ValueType avg_iir(size_t span, ValueType old_value, ValueType new_value)
{
    return (old_value * (span - 1) + new_value) / span;
}

#endif

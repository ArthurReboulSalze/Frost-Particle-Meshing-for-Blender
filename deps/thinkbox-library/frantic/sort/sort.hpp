// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

// #include <malloc.h>
// #include <process.h>
#include <boost/smart_ptr.hpp>

#pragma warning( push, 3 )
#pragma warning( disable : 4512 4100 )
// #include <tbb/task.h>
#include <algorithm>
#include <tbb/atomic.h>
#include <tbb/parallel_sort.h>
#include <tbb/tbb_thread.h>

#pragma warning( pop )

#define PARALLEL_CUTOFF 10000

// NOTE: You should include windows.h before this file to get threaded_frantic::sort()

namespace frantic {
namespace sort {

namespace detail {
struct DefaultRandIterTraits {
    template <class RandIter>
    static void* get_data_pointer( RandIter p ) {
        return *p;
    }

    template <class RandIter>
    static std::size_t get_data_size( RandIter p ) {
        return p.structure_size();
    }
};
} // namespace detail

/**
 * Swaps the n-byte structure pointed at by iterators p1 and p2. Copies in 32/64 bit chunks if it can.
 */
inline void byte_swap( void* p1, void* p2, std::size_t n ) {
    typedef std::size_t chunk_t; // Use 64-bit on x64, 32-bit on x86
                                 // TODO: Does it make more sense to use 32bit chunks? There is a significant penalty
                                 // for structures n%8 != 0 on 64bit which is fairly common. Most structs are n%4 == 0.

    chunk_t tI, *pA = reinterpret_cast<chunk_t*>( p1 ), *pB = reinterpret_cast<chunk_t*>( p2 );
    for( std::size_t i = 0; i < n / sizeof( chunk_t ); ++i, ++pA, ++pB ) {
        tI = *pA;
        *pA = *pB;
        *pB = tI;
    }

    char tC, *pCA = reinterpret_cast<char*>( pA ), *pCB = reinterpret_cast<char*>( pB );
    for( std::size_t i = 0; i < n % sizeof( chunk_t ); ++i, ++pCA, ++pCB ) {
        tC = *pCA;
        *pCA = *pCB;
        *pCB = tC;
    }
}

/**
 * Swaps the data at the two iterators. get_data_pointer is needed as sometimes this is sorting an array of pointers
 * and we just want the value from the iterator; other times we are sorting an array of objects and we need the
 * addresses of the objects themselves.
 */
template <class RandIter, class RandIterTraits>
inline void iter_swap( RandIter p1, RandIter p2, std::size_t n ) {
    byte_swap( RandIterTraits::get_data_pointer( p1 ), RandIterTraits::get_data_pointer( p2 ), n );
}

/**
 * Function override to get around the inability to pass default template arguments into function templates outside of a
 * class.
 */
template <class RandIter>
inline void iter_swap( RandIter p1, RandIter p2, std::size_t n ) {
    iter_swap<RandIter, detail::DefaultRandIterTraits>( p1, p2, n );
}

/**
 * Returns an iterator that is the median of the three supplied iterators.
 */
template <class RandIter, class Pred>
inline RandIter med3( RandIter a, RandIter b, RandIter c, const Pred& p ) {
    return p( *a, *b ) ? ( p( *b, *c ) ? b : p( *a, *c ) ? c : a ) : ( p( *c, *b ) ? b : p( *c, *a ) ? c : a );
}

/**
 * A O(n^2) sort, of the insertion variety. Use this for very small arrays.
 */
template <class RandIter, class Pred, class RandIterTraits>
inline void insertion_sort( RandIter begin, RandIter end, Pred cmp, std::size_t es ) {
    // TODO: This isn't really an insertion sort. It appears to be a bubble sort.
    for( RandIter pm = begin; pm != end; ++pm )
        for( RandIter pl = pm; pl != begin && cmp( *pl, *( pl - 1 ) ); --pl )
            iter_swap<RandIter, RandIterTraits>( pl, pl - 1, es );
}

template <class RandIter, class Pred>
inline void insertion_sort( RandIter begin, RandIter end, Pred cmp ) {
    insertion_sort<RandIter, Pred, detail::DefaultRandIterTraits>(
        begin, end, cmp, detail::DefaultRandIterTraits::get_data_size( begin ) );
}

/**
 * This function partitions the range bounded by the iterators begin and end, such that
 * it produces the best balanced partition. Returns the number of items less than the pivot,
 * and the number of items greater than the pivot. TODO: Pass in the pivot?
 */
template <class RandIter, class Pred, class ValueType, class RandIterTraits>
inline std::pair<std::size_t, std::size_t> partition( RandIter begin, RandIter end, Pred cmp, std::size_t es ) {
    std::size_t n = ( end - begin );

    if( n <= 1 )
        return std::pair<std::size_t, std::size_t>( 0, 0 );

    { // Choose the pivot and swap it into the leftmost location
        RandIter left, mid, right;
        mid = begin + ( n / 2 );
        if( n > 7 ) {
            left = begin;
            right = end - 1;
            if( n > 40 ) { /* Big arrays, pseudomedian of 9 */
                std::size_t s = ( n / 8 );
                left = med3( left, left + s, left + 2 * s, cmp );
                mid = med3( mid - s, mid, mid + s, cmp );
                right = med3( right - 2 * s, right - s, right, cmp );
            }
            mid = med3( left, mid, right, cmp ); /* Mid-size, med of 3 */
        }

        iter_swap<RandIter, RandIterTraits>( begin, mid, es );
    }

    ValueType pivot = *begin;
    RandIter pa = begin, pb = begin;
    RandIter pc = end - 1, pd = end - 1;

    for( ;; ) {
        while( pb <= pc && !cmp( pivot, *pb ) ) {
            if( !cmp( *pb, pivot ) ) {
                iter_swap<RandIter, RandIterTraits>( pa, pb, es );
                ++pa;
            }
            ++pb;
        }
        while( pc >= pb && !cmp( *pc, pivot ) ) {
            if( !cmp( pivot, *pc ) ) {
                iter_swap<RandIter, RandIterTraits>( pc, pd, es );
                --pd;
            }
            --pc;
        }
        if( pb > pc )
            break;
        iter_swap<RandIter, RandIterTraits>( pb, pc, es );
        ++pb;
        --pc;
    }

    std::size_t s;

    s = std::min<std::size_t>( pa - begin, pb - pa );
    for( RandIter a = begin, b = pb - s; s > 0; --s, ++a, ++b )
        iter_swap<RandIter, RandIterTraits>( a, b, es );

    s = std::min<std::size_t>( pd - pc, end - pd - 1 );
    for( RandIter a = pb, b = end - s; s > 0; --s, ++a, ++b )
        iter_swap<RandIter, RandIterTraits>( a, b, es );

    return std::pair<std::size_t, std::size_t>( pb - pa, pd - pc );
}

template <class RandIter, class Pred>
inline std::pair<std::size_t, std::size_t> partition( RandIter begin, RandIter end, Pred cmp ) {
    return partition<RandIter, Pred, char*, detail::DefaultRandIterTraits>(
        begin, end, cmp, detail::DefaultRandIterTraits::get_data_size( begin ) );
}

/**
 * A single threaded quicksort that degenerates into an insertion sort for small arrays. This sort
 * exists because the std::sort only works on items that can be copied by value. (see std::_Insertion_sort1 for details
 * why)
 */
template <class RandIter, class Pred, class ValueType, class RandIterTraits>
inline void sort( RandIter begin, RandIter end, Pred cmp, std::size_t elementSize ) {
    if( ( end - begin ) < 7 ) { /* Insertion sort on smallest arrays */
        insertion_sort<RandIter, Pred, RandIterTraits>( begin, end, cmp, elementSize );
        return;
    }

    std::pair<std::size_t, std::size_t> partSizes =
        frantic::sort::partition<RandIter, Pred, ValueType, RandIterTraits>( begin, end, cmp, elementSize );

    if( partSizes.first > 1 )
        frantic::sort::sort<RandIter, Pred, ValueType, RandIterTraits>( begin, begin + partSizes.first, cmp,
                                                                        elementSize );
    if( partSizes.second > 1 )
        frantic::sort::sort<RandIter, Pred, ValueType, RandIterTraits>( end - partSizes.second, end, cmp, elementSize );
}

template <class RandIter, class Pred>
inline void sort( RandIter begin, RandIter end, Pred cmp ) {
    sort<RandIter, Pred, char*, detail::DefaultRandIterTraits>( begin, end, cmp,
                                                                detail::DefaultRandIterTraits::get_data_size( begin ) );
}

namespace detail {
struct progress_info {
    tbb::tbb_thread::id m_mainThreadId;
    tbb::atomic<std::size_t> m_currentProgress;
    std::size_t m_totalProgress;

    frantic::logging::progress_logger* m_progress;
};

} // namespace detail

// Replacement for broken TBB task-based sort
// Using std::sort for compatibility
template <class RandIter, class Pred, class ValueType, class RandIterTraits>
inline void parallel_sort( RandIter begin, RandIter end, Pred cmp, frantic::logging::progress_logger& progress,
                           bool forceSingleThreaded = false ) {
    // Fallback to frantic::sort::sort because std::sort assumes value_type based swap
    // which fails for variable-stride iterators used in frantic particles
    std::size_t elementSize = RandIterTraits::get_data_size( begin );
    frantic::sort::sort<RandIter, Pred, ValueType, RandIterTraits>( begin, end, cmp, elementSize );
    progress.update_progress( 100, 100 );
}

template <class RandIter, class Pred>
inline void parallel_sort( RandIter begin, RandIter end, Pred cmp, frantic::logging::progress_logger& progress,
                           bool forceSingleThreaded = false ) {
    std::size_t elementSize = detail::DefaultRandIterTraits::get_data_size( begin );
    frantic::sort::sort<RandIter, Pred, char*, detail::DefaultRandIterTraits>( begin, end, cmp, elementSize );
    progress.update_progress( 100, 100 );
}

template <class RandIter, class Pred>
inline void parallel_sort( RandIter begin, RandIter end, Pred cmp ) {
    std::size_t elementSize = detail::DefaultRandIterTraits::get_data_size( begin );
    frantic::sort::sort<RandIter, Pred, char*, detail::DefaultRandIterTraits>( begin, end, cmp, elementSize );
}

template <class RandIter, class Pred>
inline void threaded_sort( RandIter begin, RandIter end, Pred cmp, std::size_t numThreads ) {
    std::size_t elementSize = detail::DefaultRandIterTraits::get_data_size( begin );
    frantic::sort::sort<RandIter, Pred, char*, detail::DefaultRandIterTraits>( begin, end, cmp, elementSize );
}

} // namespace sort
} // namespace frantic

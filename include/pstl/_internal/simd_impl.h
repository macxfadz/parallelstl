/*
    Copyright (c) 2017 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.




*/

#ifndef __PSTL_vector_impl_H
#define __PSTL_vector_impl_H

#include <algorithm> //for std::min
#include <type_traits>

#include "pstl_config.h"
#include "common.h"

// This header defines the minimum set of vector routines required
// to support parallel STL.

namespace __icp_algorithm {

template<class Iterator, class DifferenceType, class Function>
void simd_walk_1(Iterator first, DifferenceType n, Function f) noexcept {
__PSTL_PRAGMA_SIMD
    for(DifferenceType i = 0; i < n; ++i)
        f(first[i]);
}

template<class Iterator1, class DifferenceType, class Iterator2, class Function>
Iterator2 simd_walk_2(Iterator1 first1, DifferenceType n, Iterator2 first2, Function f) noexcept {
__PSTL_PRAGMA_SIMD
    for(DifferenceType i = 0; i < n; ++i)
        f(first1[i], first2[i]);
    return first2 + n;
}

template<class Iterator1, class DifferenceType, class Iterator2, class Iterator3, class Function>
Iterator3 simd_walk_3(Iterator1 first1, DifferenceType n, Iterator2 first2, Iterator3 first3, Function f) noexcept {
__PSTL_PRAGMA_SIMD
    for(DifferenceType i = 0; i < n; ++i)
        f(first1[i], first2[i], first3[i]);
    return first3 + n;
}

// TODO: check whether simd_first() can be used here
template<class Index, class DifferenceType, class Pred>
bool simd_or(Index first, DifferenceType n, Pred pred) noexcept {
#if __PSTL_EARLYEXIT_PRESENT
__PSTL_PRAGMA_SIMD_EARLYEXIT
    for(DifferenceType i = 0; i < n; ++i)
        if(pred(first[i]))
            return true;
    return false;
#else
    DifferenceType block_size = std::min<DifferenceType>(4, n);
    const Index last = first + n;
    while ( last != first ) {
        int flag = 1;
__PSTL_PRAGMA_SIMD_REDUCTION(&:flag)
        for ( int i = 0; i < block_size; ++i )
            if ( pred(*(first + i)) )
                flag = 0;
        if ( !flag )
            return true;

        first += block_size;
        if ( last - first >= block_size << 1 ) {
            // Double the block size.  Any unnecessary iterations can be amortized against work done so far.
            block_size <<= 1;
        }
        else {
            block_size = last - first;
        }
    }
    return false;
#endif
}

template<class Index, class DifferenceType, class Pred>
Index simd_first(Index first, DifferenceType n, Pred pred) noexcept {
#if __PSTL_EARLYEXIT_PRESENT
    DifferenceType i = 0;
__PSTL_PRAGMA_SIMD_EARLYEXIT
    for(;i < n; ++i)
        if(pred(*(first+i)))
            break;

    return first + i;
#else
    const Index last = first + n;
    // Experiments show good block sizes like this
    const int block_size = 8;
    alignas(64) int lane[block_size] = {0};
    while ( last - first >= block_size ) {
        int found = 0;
__PSTL_PRAGMA_VECTOR_UNALIGNED // Do not generate peel loop part
__PSTL_PRAGMA_SIMD_REDUCTION(|:found)
        for ( int i = 0; i < block_size; ++i ) {
            // To improve SIMD vectorization
            int t = (pred(*(first + i)));
            lane[i] = t;
            found |= t;
        }
        if ( found ) {
            int i;
            // This will vectorize
            for ( i = 0; i < block_size; ++i ) {
                if ( lane[i] ) break;
            }
            return first + i;
        }
        first += block_size;
    }
    //Keep remainder scalar
    while ( last != first ) {
        if ( pred(*(first)) ) {
            return first;
        }
        ++first;
    }
    return last;
#endif //__PSTL_EARLYEXIT_PRESENT
}

template<class Index1, class DifferenceType, class Index2, class Pred>
Index1 simd_first(Index1 first1, DifferenceType n, Index2 first2, Pred pred) noexcept {
#if __PSTL_EARLYEXIT_PRESENT
    DifferenceType i = 0;
__PSTL_PRAGMA_SIMD_EARLYEXIT
    for(;i < n; ++i)
        if(pred(first1[i], first2[i]))
            break;
    return first1+i;
#else
    Index1 last1 = first1 + n;
    // Experiments show good block sizes like this
    const int block_size = 8;
    alignas(64) int lane[block_size] = {0};
    while ( last1 - first1 >= block_size ) {
        int found = 0;
        int i;
__PSTL_PRAGMA_VECTOR_UNALIGNED // Do not generate peel loop part
__PSTL_PRAGMA_SIMD_REDUCTION(|:found)
        for ( i = 0; i < block_size; ++i ) {
            int t = pred(first1[i], first2[i]);
            lane[i] = t;
            found |= t;
        }
        if ( found ) {
            int i;
            // This will vectorize
            for ( i = 0; i < block_size; ++i ) {
                if ( lane[i] ) break;
            }
            return first1 + i;
        }
        first1 += block_size;
        first2 += block_size;
    }

    //Keep remainder scalar
    for(; last1 != first1; ++first1, ++first2)
        if ( pred(*(first1), *(first2)) )
            return first1;

    return last1;
#endif //__PSTL_EARLYEXIT_PRESENT
}

template<class Index, class DifferenceType, class Pred>
DifferenceType simd_count(Index first, DifferenceType n, Pred pred) noexcept {
    DifferenceType count = 0;
__PSTL_PRAGMA_SIMD_REDUCTION(+:count)
    for (DifferenceType i = 0; i < n; ++i)
        if (pred(*(first + i)))
            ++count;

    return count;
}

template<class InputIterator, class DifferenceType, class OutputIterator, class BinaryPredicate>
OutputIterator simd_unique_copy(InputIterator first, DifferenceType n, OutputIterator result, BinaryPredicate pred) noexcept {
    if (n == 0)
        return result;

    DifferenceType cnt = 1;
    result[0] = first[0];

__PSTL_PRAGMA_SIMD
    for (DifferenceType i = 1; i < n; ++i) {
__PSTL_PRAGMA_SIMD_ORDERED_MONOTONIC(cnt:1)
        if (!pred(first[i], first[i - 1])) {
            result[cnt] = first[i];
            ++cnt;
        }
    }
    return result + cnt;
}

template<class InputIterator, class DifferenceType, class OutputIterator>
OutputIterator simd_copy_n(InputIterator first, DifferenceType n, OutputIterator result) noexcept {
__PSTL_PRAGMA_SIMD
    for(DifferenceType i = 0; i < n; ++i)
        result[i] = first[i];
    return result+n;
}

template<class InputIterator, class DifferenceType, class OutputIterator, class UnaryPredicate>
OutputIterator simd_copy_if(InputIterator first, DifferenceType n, OutputIterator result, UnaryPredicate pred) noexcept {
    DifferenceType cnt = 0;

__PSTL_PRAGMA_SIMD
    for(DifferenceType i = 0; i < n; ++i) {
        __PSTL_PRAGMA_SIMD_ORDERED_MONOTONIC(cnt:1)
            if(pred(first[i])) {
                result[cnt] = first[i];
                ++cnt;
            }
    }
    return result + cnt;
}

template<class InputIterator, class DifferenceType, class BinaryPredicate>
DifferenceType simd_calc_mask_2(InputIterator first, DifferenceType n, bool* __restrict mask, BinaryPredicate pred) noexcept {
    DifferenceType count = 0;

__PSTL_PRAGMA_SIMD_REDUCTION(+:count)
    for (DifferenceType i = 0; i < n; ++i) {
        mask[i] = !pred(first[i], first[i - 1]);
        count += mask[i];
    }
    return count;
}

template<class InputIterator, class DifferenceType, class UnaryPredicate>
DifferenceType simd_calc_mask_1(InputIterator first, DifferenceType n, bool* __restrict mask, UnaryPredicate pred) noexcept {
    DifferenceType count = 0;

__PSTL_PRAGMA_SIMD_REDUCTION(+:count)
    for (DifferenceType i = 0; i < n; ++i) {
        mask[i] = pred(first[i]);
        count += mask[i];
    }
    return count;
}

template<class InputIterator, class DifferenceType, class OutputIterator>
void simd_copy_by_mask(InputIterator first, DifferenceType n, OutputIterator result, bool* __restrict mask) noexcept {
    DifferenceType cnt = 0;
__PSTL_PRAGMA_SIMD
    for (DifferenceType i = 0; i < n; ++i) {
__PSTL_PRAGMA_SIMD_ORDERED_MONOTONIC(cnt:1)
        if (mask[i]) {
            result[cnt] = first[i];
            ++cnt;
        }
    }
}

template<class Index, class DifferenceType, class T>
Index simd_fill_n(Index first, DifferenceType n, const T& value) noexcept {
__PSTL_PRAGMA_SIMD
    for (DifferenceType i = 0; i < n; ++i)
        first[i] = value;
    return first + n;
}

template<class Index, class T>
void simd_fill(Index first, Index last, const T& value) noexcept {
    simd_fill_n(first, last - first, value);
}

template<class Index, class DifferenceType, class Generator>
Index simd_generate_n(Index first, DifferenceType size, Generator g) noexcept {
__PSTL_PRAGMA_SIMD
    for (DifferenceType i = 0; i < size; ++i)
        first[i] = g();
    return first + size;
}

template<class Index, class Generator>
void simd_generate(Index first, Index last, Generator g) noexcept {
    simd_generate_n(first, last - first, g);
}

template<class Index, class BinaryPredicate>
Index simd_adjacent_find(Index first, Index last, BinaryPredicate pred, bool or_semantic) noexcept {
    if(last - first < 2)
        return last;

#if __PSTL_EARLYEXIT_PRESENT
    //Some compiler versions fail to compile the following loop when iterators are used. Indices are used instead
    size_t i = 0, n = last-first-1;
__PSTL_PRAGMA_SIMD_EARLYEXIT
    for(; i < n; ++i)
        if(pred(first[i], first[i+1]))
            break;

    return i < n ? first + i : last;
#else
    // Experiments show good block sizes like this
    //TODO: to consider tuning block_size for various data types
    const int block_size = 8;
    alignas(64) int lane[block_size] = {0};
    while ( last - first >= block_size ) {
        int found = 0, i;
__PSTL_PRAGMA_VECTOR_UNALIGNED // Do not generate peel loop part
__PSTL_PRAGMA_SIMD_REDUCTION(|:found)
        for ( i = 0; i < block_size-1; ++i ) {
            //TODO: to improve SIMD vectorization
            const int t = pred(*(first + i), *(first + i + 1));
            lane[i] = t;
            found |= t;
        }

        //Process a pair of elements on a boundary of a data block
        if(first + block_size < last && pred(*(first + i), *(first + i + 1)))
            lane[i] = found = 1;

        if ( found ) {
            if(or_semantic)
                return first;
            int i;
            // This will vectorize
            for ( i = 0; i < block_size; ++i )
                if ( lane[i] ) break;
            return first + i; //As far as found is true a result (lane[i] is true) is guaranteed
        }
        first += block_size;
    }
    //Process the rest elements
    for (; last - first > 1; ++first)
        if(pred(*first, *(first+1)))
            return first;

    return last;
#endif
}

template<class Index1, class Index2, class BinaryPredicate>
Index1 simd_search(Index1 first, Index1 last, Index2 s_first, Index2 s_last, BinaryPredicate p, bool b_first) noexcept {
    auto  n2 = s_last - s_first;
    if(n2 < 1)
        return last;

    auto  n1 = last - first;
    if(n1 < n2)
        return last;

    Index1 result = last;
    for(auto i = n1-n2; i >= 0; --i, ++first) {
        if(simd_first(s_first, s_last-s_first, first, not_pred<BinaryPredicate>(p)) == s_last) {//subsequence was found
            result = first;
            if(b_first) //first occurrence semantic
                break;
        }
    }
    return result;
}

template<typename InputIterator1, typename DifferenceType, typename InputIterator2, typename T, typename BinaryOperation>
T simd_transform_reduce(InputIterator1 first1, DifferenceType n, InputIterator2 first2, T init, BinaryOperation binary_op) noexcept {
__PSTL_PRAGMA_SIMD_REDUCTION(+:init)
    for(DifferenceType i = 0; i < n; ++i)
        init += binary_op(first1[i], first2[i]);
    return init;
};

template<typename InputIterator, typename DifferenceType, typename T, typename UnaryOperation>
T simd_transform_reduce(InputIterator first, DifferenceType n, T init, UnaryOperation unary_op) noexcept {
__PSTL_PRAGMA_SIMD_REDUCTION(+:init)
    for(DifferenceType i = 0; i < n; ++i)
        init += unary_op(first[i]);
    return init; 
};

} // namespace __icp_algorithm
#endif /* __PSTL_vector_impl_H */

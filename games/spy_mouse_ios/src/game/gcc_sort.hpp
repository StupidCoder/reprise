// The original's std::sort (libstdc++ 4.2: introsort with a median-of-three pivot, heapsort
// past the depth limit, insertion sort to finish). It is not stable, and some of the game's
// orders depend on how it arranges equal keys (an infinite level's objects are made in the
// order of its entities sorted by x, MapEntityContainer). Reproduced step by step so equal keys
// come out as on the device.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace doc::game {

namespace gcc_sort_detail {

template <class T, class Less>
const T& median(const T& a, const T& b, const T& c, Less less) {
    if (less(a, b)) {
        if (less(b, c)) return b;
        if (less(a, c)) return c;
        return a;
    }
    if (less(a, c)) return a;
    if (less(b, c)) return c;
    return b;
}

template <class T, class Less>
std::ptrdiff_t unguardedPartition(std::vector<T>& v, std::ptrdiff_t first, std::ptrdiff_t last, T pivot, Less less) {
    for (;;) {
        while (less(v[std::size_t(first)], pivot)) ++first;
        --last;
        while (less(pivot, v[std::size_t(last)])) --last;
        if (!(first < last)) return first;
        std::swap(v[std::size_t(first)], v[std::size_t(last)]);
        ++first;
    }
}

template <class T, class Less>
void adjustHeap(std::vector<T>& v, std::ptrdiff_t first, std::ptrdiff_t hole, std::ptrdiff_t length, T value, Less less) {
    const std::ptrdiff_t top = hole;
    std::ptrdiff_t second = 2 * hole + 2;
    while (second < length) {
        if (less(v[std::size_t(first + second)], v[std::size_t(first + second - 1)])) --second;
        v[std::size_t(first + hole)] = v[std::size_t(first + second)];
        hole = second;
        second = 2 * (second + 1);
    }
    if (second == length) {
        v[std::size_t(first + hole)] = v[std::size_t(first + second - 1)];
        hole = second - 1;
    }
    std::ptrdiff_t parent = (hole - 1) / 2;
    while (hole > top && less(v[std::size_t(first + parent)], value)) {
        v[std::size_t(first + hole)] = v[std::size_t(first + parent)];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    v[std::size_t(first + hole)] = value;
}

template <class T, class Less>
void heapSortRange(std::vector<T>& v, std::ptrdiff_t first, std::ptrdiff_t last, Less less) {
    const std::ptrdiff_t n = last - first;
    if (n >= 2)
        for (std::ptrdiff_t parent = (n - 2) / 2;; --parent) {
            adjustHeap(v, first, parent, n, v[std::size_t(first + parent)], less);
            if (parent == 0) break;
        }
    for (std::ptrdiff_t middle = last; middle - first > 1;) {
        --middle;
        T value = v[std::size_t(middle)];
        v[std::size_t(middle)] = v[std::size_t(first)];
        adjustHeap(v, first, 0, middle - first, value, less);
    }
}

template <class T, class Less>
void introsortLoop(std::vector<T>& v, std::ptrdiff_t first, std::ptrdiff_t last, int depth, Less less) {
    while (last - first > 16) {
        if (depth == 0) {
            heapSortRange(v, first, last, less);
            return;
        }
        --depth;
        const T pivot = median(v[std::size_t(first)], v[std::size_t(first + (last - first) / 2)],
                               v[std::size_t(last - 1)], less);
        const std::ptrdiff_t cut = unguardedPartition(v, first, last, pivot, less);
        introsortLoop(v, cut, last, depth, less);
        last = cut;
    }
}

template <class T, class Less>
void insertionSort(std::vector<T>& v, std::ptrdiff_t first, std::ptrdiff_t last, Less less) {
    for (std::ptrdiff_t i = first + 1; i < last; ++i) {
        T value = v[std::size_t(i)];
        if (less(value, v[std::size_t(first)])) {
            for (std::ptrdiff_t k = i; k > first; --k) v[std::size_t(k)] = v[std::size_t(k - 1)];
            v[std::size_t(first)] = value;
        } else {
            std::ptrdiff_t j = i;
            while (less(value, v[std::size_t(j - 1)])) {
                v[std::size_t(j)] = v[std::size_t(j - 1)];
                --j;
            }
            v[std::size_t(j)] = value;
        }
    }
}

}  // namespace gcc_sort_detail

template <class T, class Less>
void gccSort(std::vector<T>& v, Less less) {
    using namespace gcc_sort_detail;
    const std::ptrdiff_t n = std::ptrdiff_t(v.size());
    if (n < 2) return;
    int lg = 0;
    for (std::ptrdiff_t k = n; k != 1; k >>= 1) ++lg;
    introsortLoop(v, 0, n, lg * 2, less);
    if (n > 16) {
        insertionSort(v, 0, 16, less);
        for (std::ptrdiff_t i = 16; i < n; ++i) {  // unguarded insertion
            T value = v[std::size_t(i)];
            std::ptrdiff_t j = i;
            while (less(value, v[std::size_t(j - 1)])) {
                v[std::size_t(j)] = v[std::size_t(j - 1)];
                --j;
            }
            v[std::size_t(j)] = value;
        }
    } else {
        insertionSort(v, 0, n, less);
    }
}

}  // namespace doc::game

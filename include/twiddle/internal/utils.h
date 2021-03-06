#ifndef TWIDDLE_INTERNAL_UTILS_H
#define TWIDDLE_INTERNAL_UTILS_H

#include <float.h>
#include <math.h>

/* Number of bytes per cache line */
#ifndef TW_CACHELINE
#define TW_CACHELINE 64
#endif

#define TW_DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define TW_ALLOC_TO_CACHELINE(size)                                            \
  ((TW_DIV_ROUND_UP(size, TW_CACHELINE) * TW_CACHELINE))

#define TW_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

#define TW_BITOP_ADDR(x) "+m"(*(volatile long *)(x))

#define TW_BITS_IN_WORD 8
#define TW_BIT_POS(x) (1 << (x % TW_BITS_IN_WORD))
#define TW_BYTE_POS(x) (x / TW_BITS_IN_WORD)

#define tw_likely(x) __builtin_expect((x), 1)
#define tw_unlikely(x) __builtin_expect((x), 0)

#define tw_min(a, b) ((a < b) ? a : b)
#define tw_max(a, b) ((a < b) ? b : a)

#define tw_almost_equal(a, b) (fabs(a - b) < FLT_EPSILON)

#endif /* TWIDDLE_INTERNAL_UTILS_H */

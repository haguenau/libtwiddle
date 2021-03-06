#include <assert.h>
#include <math.h>
#include <string.h>
#include <x86intrin.h>

#include <twiddle/hyperloglog/hyperloglog.h>
#include <twiddle/hyperloglog/hyperloglog_simd.h>
#include <twiddle/hash/metrohash.h>
#include <twiddle/internal/utils.h>
#include <twiddle/internal/simd.h>

struct tw_hyperloglog *tw_hyperloglog_new(uint8_t precision)
{
  if (precision < TW_HLL_MIN_PRECISION || precision > TW_HLL_MAX_PRECISION) {
    return NULL;
  }

  struct tw_hyperloglog *hll = calloc(1, sizeof(struct tw_hyperloglog));
  if (!hll) {
    return NULL;
  }

  size_t alloc_size = TW_ALLOC_TO_CACHELINE(1 << precision) * sizeof(uint8_t);

  if ((hll->registers = aligned_alloc(TW_CACHELINE, alloc_size)) == NULL) {
    free(hll);
    return NULL;
  }

  memset(hll->registers, 0, alloc_size);

  hll->precision = precision;

  return hll;
}

void tw_hyperloglog_free(struct tw_hyperloglog *hll)
{
  assert(hll);
  free(hll->registers);
  free(hll);
}

struct tw_hyperloglog *tw_hyperloglog_copy(const struct tw_hyperloglog *src,
                                           struct tw_hyperloglog *dst)
{
  assert(src && dst);

  const uint8_t precision = src->precision;
  if (precision != dst->precision) {
    return NULL;
  }

  dst->precision = precision;

  const uint32_t n_registers = 1 << precision;
  memcpy(dst->registers, src->registers,
         n_registers * TW_BYTES_PER_HLL_REGISTER);

  return dst;
}

struct tw_hyperloglog *tw_hyperloglog_clone(const struct tw_hyperloglog *src)
{
  assert(src);

  struct tw_hyperloglog *dst = tw_hyperloglog_new(src->precision);
  if (dst == NULL) {
    return NULL;
  }

  return tw_hyperloglog_copy(src, dst);
}

void tw_hyperloglog_add(struct tw_hyperloglog *hll, const void *key,
                        size_t key_size)
{
  assert(hll && key && key_size > 0);
  const uint64_t hash = tw_metrohash_64(TW_HLL_DEFAULT_SEED, key, key_size);
  const uint8_t precision = hll->precision;
  const uint32_t bucket_idx = hash >> (64 - precision);
  const uint8_t leading_zeros = (__builtin_clzll(hash << precision |
                                                 (1 << (precision - 1))) +
                                 1),
                old_val = hll->registers[bucket_idx];
  hll->registers[bucket_idx] =
      (leading_zeros > old_val) ? leading_zeros : old_val;
}

extern double estimate(uint8_t precision, uint32_t n_zeros, float inverse_sum);

#ifdef USE_AVX2
extern void hyperloglog_count_avx2(const uint8_t *registers,
                                   uint32_t n_registers, float *inverse_sum,
                                   uint32_t *n_zeros);
#elif defined USE_AVX
extern void hyperloglog_count_avx(const uint8_t *registers,
                                  uint32_t n_registers, float *inverse_sum,
                                  uint32_t *n_zeros);
#else
extern void hyperloglog_count_port(const uint8_t *registers,
                                   uint32_t n_registers, float *inverse_sum,
                                   uint32_t *n_zeros);
#endif

double tw_hyperloglog_count(const struct tw_hyperloglog *hll)
{
  assert(hll);

  const uint8_t precision = hll->precision;
  const uint32_t n_registers = 1 << precision;
  uint32_t n_zeros = 0;
  float inverse_sum = 0.0;

#ifdef USE_AVX2
  hyperloglog_count_avx2(hll->registers, n_registers, &inverse_sum, &n_zeros);
#elif defined USE_AVX
  hyperloglog_count_avx(hll->registers, n_registers, &inverse_sum, &n_zeros);
#else
  hyperloglog_count_port(hll->registers, n_registers, &inverse_sum, &n_zeros);
#endif

  return estimate(precision, n_zeros, inverse_sum);
}

bool tw_hyperloglog_equal(const struct tw_hyperloglog *a,
                          const struct tw_hyperloglog *b)
{
  assert(a && b);

  const uint8_t precision = a->precision;

  if (precision != b->precision) {
    return false;
  }

  const uint32_t n_registers = 1 << precision;

#define HLL_EQ_LOOP(simd_t, simd_load, simd_equal)                             \
  for (size_t i = 0; i < n_registers / (sizeof(simd_t)); ++i) {                \
    simd_t *a_addr = (simd_t *)a->registers + i,                               \
           *b_addr = (simd_t *)b->registers + i;                               \
    if (!simd_equal(simd_load(a_addr), simd_load(b_addr))) {                   \
      return false;                                                            \
    }                                                                          \
  }

/* AVX512 does not have movemask_epi8 equivalent, fallback to AVX2 */
#ifdef USE_AVX2
  HLL_EQ_LOOP(__m256i, _mm256_load_si256, tw_mm256_equal)
#elif defined USE_AVX
  HLL_EQ_LOOP(__m128i, _mm_load_si128, tw_mm_equal)
#else
  for (size_t i = 0; i < n_registers; ++i) {
    if (a->registers[i] != b->registers[i]) {
      return false;
    }
  }
#endif

  return true;
}

struct tw_hyperloglog *tw_hyperloglog_merge(const struct tw_hyperloglog *src,
                                            struct tw_hyperloglog *dst)
{
  assert(src && dst);

  const uint8_t precision = src->precision;

  if (precision != dst->precision) {
    return NULL;
  }

  const uint32_t n_registers = 1 << precision;

#define HLL_MAX_LOOP(simd_t, simd_load, simd_max, simd_store)                  \
  for (size_t i = 0; i < n_registers / sizeof(simd_t); ++i) {                  \
    simd_t *src_vec = (simd_t *)src->registers + i,                            \
           *dst_vec = (simd_t *)dst->registers + i;                            \
    const simd_t res = simd_max(simd_load(src_vec), simd_load(dst_vec));       \
    simd_store(dst_vec, res);                                                  \
  }

#ifdef USE_AVX512
  HLL_MAX_LOOP(__m512i, _mm512_load_si512, _mm512_max_epu8, _mm512_store_si512)
#elif defined USE_AVX2
  HLL_MAX_LOOP(__m256i, _mm256_load_si256, _mm256_max_epu8, _mm256_store_si256)
#elif defined USE_AVX
  HLL_MAX_LOOP(__m128i, _mm_load_si128, _mm_max_epu8, _mm_store_si128)
#else
  for (size_t i = 0; i < n_registers; ++i) {
    dst->registers[i] = tw_max(src->registers[i], dst->registers[i]);
  }
#endif

  return dst;
}

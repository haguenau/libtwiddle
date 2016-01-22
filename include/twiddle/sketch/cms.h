#ifndef TWIDDLE_COUNTMINSKETCH_H
#define TWIDDLE_COUNTMINSKETCH_H

#include <stdbool.h>
#include <stdint.h>

struct tw_cms_info {
  uint64_t hash_seed;
  uint16_t k; // number of functions
  uint32_t n_registers;
};

#define TW_CMS_DEFAULT_SEED 3781869495ULL

struct tw_cms {
  struct tw_cms_info info;
  uint32_t registers[];
};

struct tw_cms *tw_cms_new(uint32_t n_registers, uint16_t k);

void tw_cms_free(struct tw_cms *cms);

void tw_cms_add(struct tw_cms *cms, size_t key_size, const char *key);

uint32_t tw_cms_count(const struct tw_cms *cms, size_t key_size,
                      const char *key);

#endif /* TWIDDLE_COUNTMINSKETCH_H */

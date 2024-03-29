#ifndef __INTERSECTION_H__
#define __INTERSECTION_H__

#include "word_to_docids.h"

#define MAX_QUERY_WORDS 8
#define MAX_INSERSECTION_DOCS 64

unsigned word_cnt;

// This program uses variable sized arrays. The size of the array is stored in
// the first element of the array.
// Source:
// https://github.com/wenischlab/MicroSuite/blob/master/src/SetAlgebra/intersection_service/src/intersection.cc
void compute_intersection(const uint64_t *word_one, const uint64_t *word_two, uint64_t *result) {
  unsigned word_one_size = word_one[0];
  unsigned word_two_size = word_two[0];
  word_one++;
  word_two++;

  unsigned i = 0, j = 0, k = 0;

  while( (i < word_one_size) && (j < word_two_size) ) {
    if (word_one[i] < word_two[j]) {
      i++;
    } else if (word_two[j] < word_one[i]) {
      j++;
    } else {
      result[1+k] = word_one[i];
      k++;
      i++;
      j++;
    }
  }
  result[0] = k;
}

void load_docs() {
  word_cnt = word_to_docids_bin[0];
  uint64_t *p = &word_to_docids_bin[1];
  for (unsigned i = 0; i < word_cnt; i++) {
    if (i % 40 == 0) printf("loaded %d words\n", i);
    uint64_t *arr_size = p++;
    word_to_docids[i] = arr_size;
    // XXX load them into the cache
    for (unsigned j = 1; j <= *arr_size; j++)
      if (word_to_docids[i][j] == 0)
        printf("DocID should not be zero.\n");;
    p += *arr_size;
  }
}

#endif // __INTERSECTION_H__
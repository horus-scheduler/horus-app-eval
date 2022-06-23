#ifndef HL_HASHTABLE_H
#define HL_HASHTABLE_H

#include <stdint.h>
#include <stdbool.h>
#include "ll.h"

typedef struct hashtable_item_s { 
  uint32_t key;
  void *value;
} hashtable_item;


typedef struct _hashtable_s hashtable_t;

#define HT_SIZE_MIN 5


hashtable_t *ht_create(size_t initial_size, size_t max_size, ht_free_item_callback_t free_item_cb);

void print_table(hashtable_t *table);

int ht_init(hashtable_t *table, size_t initial_size, size_t max_size, ht_free_item_callback_t free_item_cb);

/**
 * Get the value stored at a specific key
 */
void *ht_get(hashtable_t *table, uint32_t key);

/**
 * Not implemented yet.
 */
int ht_exists(hashtable_t *table, void *key, size_t klen);


int ht_insert(hashtable_t *table, uint32_t key, void *data);


/**
 * Delete the value stored at a specific key
 * prev_data : Container to copy the value of deleted node.
 * prev_len  : If not 0, will deepcopy the data of node before deletion otherwise no copy is performed.
 * return 0 on success, -1 otherwise
 */
int ht_delete(hashtable_t *table, uint32_t key, void *prev_data, size_t prev_len);

/* Modify the value of the given key if exists otherwise returns error -1 */
int ht_modify(hashtable_t *table, uint32_t key, void *data);

void ht_test(void);

#endif

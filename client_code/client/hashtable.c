#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <strings.h>
#include <sched.h>
#include "hashtable.h"
#include "ll.h"
#include "atomic_defs.h"

// NOTE : order here matters (and also numbering)
typedef enum {
    HT_STATUS_CLEAR = 0,
    HT_STATUS_WRITE = 1,
    HT_STATUS_GROW  = 2,
    HT_STATUS_IDLE  = 3,
    HT_STATUS_READ  = 4
}  ht_status_t;



void item_printer(void *n) {
    hashtable_item *input_item = (hashtable_item *)n;
    printf(" (req_id: %d ", input_item->key);
    if (input_item->value == NULL)
        printf(" , val: NULL)");
    else 
        printf(" , val: %lu)", (uint64_t)input_item->value);
}

void item_teardown(void *n) {
    hashtable_item *input_item = (hashtable_item *)n;
    input_item->key = -1; 
    input_item->value = NULL;
}

int item_search_func(void *n, uint32_t key) {
    hashtable_item *input_item = (hashtable_item *)n;
    return (input_item->key == key);
}


struct _hashtable_s {
    size_t size;
    size_t max_size;
    size_t count;
    ht_status_t status;
    uint32_t seed;
    ll_t **items;
    ht_free_item_callback_t free_item_cb;
    pthread_mutex_t iterator_lock;
};


hashtable_t * ht_create(size_t initial_size, size_t max_size, ht_free_item_callback_t cb)
{
    //printf("Before alloc\n");
    hashtable_t *table = (hashtable_t *)calloc(1, sizeof(hashtable_t));
    //printf("Before init\n");
    if (table && ht_init(table, initial_size, max_size, cb) != 0) {
        free(table);
        return NULL;
    }
    
    

    return table;
}

int
ht_init(hashtable_t *table,
        size_t initial_size,
        size_t max_size,
        ht_free_item_callback_t cb)
{
    table->size = initial_size > HT_SIZE_MIN ? initial_size : HT_SIZE_MIN;
    table->max_size = max_size;
    //printf("Before init alloc\n");
    table->items = (ll_t **)calloc(table->size, sizeof(ll_t *));
    //printf("Before list allocs\n");
    for (int i=0; i < table->size; i++) {
        table->items[i] = ll_new(item_teardown);
        table->items[i]->val_printer = item_printer;
    }
    //printf("After list allocs\n");
    if (!table->items)
        return -1;

    table->status = HT_STATUS_IDLE;

    table->free_item_cb = cb;
#ifdef BSD
    table->seed = arc4random()%UINT32_MAX;
#else
    table->seed = random()%UINT32_MAX;
#endif
    //printf("Before mutex init\n");
    pthread_mutex_init(&(table->iterator_lock), 0);
    //printf("After mutex init\n");
    return 0;
}

static inline size_t ht_get_index(hashtable_t *table, uint32_t key)
{
    //size_t hash = table->seed + key;
    return key%(table->size);
}

int ht_insert(hashtable_t *table, uint32_t key, void *data){
    size_t index = ht_get_index(table, key);
    //printf("Got idx %d\n", index);
    ll_t *list  = table->items[index];
    
    printf("Checking existing item \n");
    hashtable_item *existing_item = ht_get(table, key);
    printf("after check \n");
    if (existing_item != NULL) { // if key already exists
        printf("Existing in HT %d\n", key);
        existing_item->value = data;    
    } else {
        int ret;
        hashtable_item *item = (hashtable_item *)calloc(1, sizeof(hashtable_item));
        item->key = key;
        item->value = data;
        ret = ll_insert_last(list, item);
        if (ret == -1) {
            return ret;
        }
    }
    ATOMIC_INCREMENT(table->count);
    
    return 0;
}

int ht_modify(hashtable_t *table, uint32_t key, void *data) {
    size_t index = ht_get_index(table, key);
    //printf("Got idx %d\n", index);
    ll_t *list  = table->items[index];

    
    
    //printf("Checking existing item \n");
    hashtable_item *existing_item = ht_get(table, key);
    //printf("after check \n");
    if (existing_item != NULL) { // if key already exists
        //printf("Existing in HT %d\n", key);
        existing_item->value = data;
        return 0;    
    } 
    return -1;
}

void *ht_get(hashtable_t *table, uint32_t key) {
    size_t index = ht_get_index(table, key);

    ll_t *list  = table->items[index];
    hashtable_item *item = (hashtable_item *)ll_search(list, item_search_func, key);
    if (item == NULL) {
        return item;
    }
    return item;
}

int ht_delete(hashtable_t *table, uint32_t key, void *prev_data, size_t prev_len){
    int ret;
    size_t index = ht_get_index(table, key);
    ll_t *list  = table->items[index];
    ret = ll_remove_search(list, item_search_func, key, prev_data, prev_len);
    if (ret != -1) {
        ATOMIC_DECREMENT(table->count);
        return ret;
    }
    
    return -1;
}

void print_table(hashtable_t *table) {
    printf("\nPrinting HT \n");
    for (int i=0; i < table->size; i++) {
        ll_print(*(table->items[i]));
    }
    printf("\n\n");
}

void ht_test() {
    //printf("Before CREATE");
    hashtable_t *table = ht_create(10, 0, NULL);
    int test_var [100];
    for (int i=0; i < 100; i ++) {
        test_var[i] = i+1000;
    }
    //printf("Before insert\n");
    ht_insert(table, 0, &test_var[0]);
    //printf("After insert\n");
    ht_insert(table, 10, &test_var[10]);
    ht_insert(table, 11, &test_var[11]);
    ht_insert(table, 16, &test_var[16]);
    ht_insert(table, 26, &test_var[26]);
    ll_print(*(table->items[0]));
    ll_print(*(table->items[1]));
    ll_print(*(table->items[6]));
    int* retrived = ht_get(table, 0);
    int* retrived1 = ht_get(table, 16);
    int* retrived2 = ht_get(table, 17);
    //printf("retrived key0: %d\n", *retrived);
    if (retrived1 == NULL) {
        printf("NULLL\n");
    } else {
        printf("retrived key1: %d\n", *retrived1);    
    }
    if (retrived2 == NULL) {
        printf("NULLL\n");
    } else {
        printf("retrived key2: %d\n", *retrived2);    
    }
    hashtable_item deleted;
    
    ht_delete(table, 16, &deleted, sizeof(deleted));
    
    printf("Deleted: {%d : %d}\n", deleted.key, *(int *)(deleted.value));
    int ret = ht_delete(table, 13, &deleted, sizeof(deleted));
    if (ret == -1){
        printf("DELETE FAILED!");
    }
    
    ll_print(*(table->items[0]));
    ll_print(*(table->items[1]));
    ll_print(*(table->items[6]));
    
    // hashtable_item *item1 = malloc(sizeof *item1); 
    // item1->key=50;
    // hashtable_item *item2 = malloc(sizeof *item2); 
    // item2->key=10;
    // ll_insert_first(table->items[0], item1);
    // ll_insert_first(table->items[1], item2);
    // ll_print(*(table->items[0]));
    // ll_print(*(table->items[1]));
}

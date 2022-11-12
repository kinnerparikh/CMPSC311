#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
	if (num_entries < 2 || num_entries > 4096 || cache_enabled()) {
		return -1;
	}
	cache = calloc(num_entries, sizeof(cache_entry_t));
	cache_size = num_entries;

    return 1;
}

int cache_destroy(void) {
	if (!cache_enabled()) {
		return -1;
	}
	free(cache);
	cache = NULL;
	cache_size = 0;
	num_queries = 0;
	num_hits = 0;
    return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
	num_queries++;
	if (!cache_enabled() || buf == NULL || disk_num < 0 || block_num < 0 || disk_num > 15 || block_num > 255) {
		return -1;
	}

	for (int i = 0; i < cache_size; i++) {
		if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
			num_hits++;
			memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
			(cache+i)->num_accesses++;
			return 1;
		}
	}
	
  	return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
	for (int i = 0; i < cache_size; i++) {
		if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
			memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
			cache[i].num_accesses++;
			return;
		}
	}
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
	if (!cache_enabled() || buf == NULL || disk_num < 0 || block_num < 0 || disk_num > 15 || block_num > 255) {
		return -1;
	}
	int least_used = 0;
	for(int i = 0; i < cache_size; i++) {
		if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
			return -1;
		}
		if (cache[i].num_accesses < cache[least_used].num_accesses) {
			least_used = i;
		}
	}
	cache[least_used].valid = true;
	cache[least_used].disk_num = disk_num;
	cache[least_used].block_num = block_num;
	memcpy(cache[least_used].block, buf, JBOD_BLOCK_SIZE);
	cache[least_used].num_accesses = 1;
  	return 1;
}

bool cache_enabled(void) {
	return cache != NULL && cache_size > 0;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}

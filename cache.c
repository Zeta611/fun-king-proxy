#include <limits.h>
#include <semaphore.h>
#include <string.h>

#include "cache.h"
#include "utils.h"

#define P(s) sem_wait(s)
#define V(s) sem_post(s)

sem_t mutex, w;
static int readcnt = 0;

struct cache Make_cache(void)
{
	if (sem_init(&mutex, 0, 1) < 0) {
		unix_error("sem_init");
	}
	if (sem_init(&w, 0, 1) < 0) {
		unix_error("sem_init");
	}

	struct ca_item *head = NULL, *tail = NULL;
	if ((head = malloc(sizeof *head)) == NULL) {
		unix_error("malloc");
	}
	if ((tail = malloc(sizeof *tail)) == NULL) {
		unix_error("malloc");
	}

	head->size = head->cnt = 0;
	head->next = tail;
	tail->prev = head;
	head->prev = tail->next = NULL;

	struct cache c = {head, tail};
	return c;
}

int get_cache(const struct cache *cache, const char *key, void *item,
	      size_t *size)
{
	int found = 0;

	P(&mutex);
	if (++readcnt == 1) {
		P(&w);
	}
	V(&mutex);

	/********** CRITICAL SECTION **********/
	for (struct ca_item *it = cache->head->next; it != cache->tail;
	     it = it->next) {
		if (strcmp(key, it->key) == 0) {
			memcpy(item, it->item, it->size);
			*size = it->size;
			++it->cnt;
			found = 1;
			break;
		}
	}
	/**************************************/

	P(&mutex);
	if (--readcnt == 0) {
		V(&w);
	}
	V(&mutex);

	return found ? 0 : -1;
}

int put_cache(struct cache *cache, const char *key, const void *item,
	      size_t size)
{
	if (size > MAX_OBJECT_SIZE) {
		return -1;
	}

	struct ca_item *new_it = malloc(sizeof *new_it);
	if (new_it == NULL) {
		goto malloc_err;
	}

	void *item_cpy = malloc(size);
	if (item_cpy == NULL) {
		free(new_it);
		goto malloc_err;
	}

	void *key_cpy = strdup(key);
	if (key_cpy == NULL) {
		free(new_it);
		free(item_cpy);
		goto malloc_err;
	}

	memcpy(item_cpy, item, size);

	new_it->size = size;
	new_it->cnt = 1;
	new_it->key = key_cpy;
	new_it->item = item_cpy;

	P(&w);
	/********** CRITICAL SECTION **********/
	size_t free_space = MAX_CACHE_SIZE - cache->head->size;
	while (free_space < size) {
		/* Evict */
		struct ca_item *it, *cand;
		int min = INT_MAX;
		for (it = cache->head->next; it != cache->tail; it = it->next) {
			if (it->cnt < min) {
				min = it->cnt;
				cand = it;
			}
		}

		cache->head->size -= cand->size;
		--cache->head->cnt;

		cand->prev->next = cand->next;
		cand->next->prev = cand->prev;

		free(cand->key);
		free(cand->item);
		free(cand);
	}

	new_it->next = cache->head->next;
	cache->head->next = new_it;
	new_it->prev = cache->head;
	new_it->next->prev = new_it;

	cache->head->size += size;
	++cache->head->cnt;
	/**************************************/
	V(&w);

	return 0;

malloc_err:
	msg_unix_error("malloc");
	return -1;
}

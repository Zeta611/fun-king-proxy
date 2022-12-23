#ifndef __CACHE_H__
#define __CACHE_H__

#include <stdlib.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct ca_item {
	size_t size; /* cache size for head, item size for items */
	int cnt;     /* item count for head, touched count for items */
	char *key;
	void *item;
	struct ca_item *next, *prev;
};

struct cache {
	struct ca_item *head, *tail;
};

struct cache Make_cache(void);
int get_cache(const struct cache *cache, const char *key, void *item,
	      size_t *size);
int put_cache(struct cache *cache, const char *key, const void *item,
	      size_t size);

#endif /* __CACHE_H__ */

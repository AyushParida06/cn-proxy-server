// cache module - Aarushi

#include<stdio.h>
#include<string.h>
#include<time.h>

struct CacheEntry {
    char url[512];        // the key: which page this is
    char response[8192];  // the saved reply
    time_t time_stored;   // when we saved it (used later for the TTL check)
};

// the cache: room for 10 entries, and a counter of how many are used
struct CacheEntry cache[10];
int cache_count = 0;

int main(){
	strcpy(cache[0].url, "http://a.com/");
	strcpy(cache[0].response, "page A");
	cache[0].time_stored = time(NULL);

	strcpy(cache[1].url, "http://b.com/");
	strcpy(cache[1].response, "page B");
	cache[1].time_stored = time(NULL);
	cache_count = 2;
	for (int i=0; i<cache_count;i++) {
		printf("%s %s %ld\n", cache[i].url, cache[i].response, (long)cache[i].time_stored);
	}
	return 0;
}
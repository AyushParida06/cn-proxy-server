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

void cache_put(const char *url, const char *response) {
	printf("cache_put called with %s\n", url);
	for(int i=0;i<cache_count;i++){
		if (strcmp(cache[i].url, url) == 0) {
			strncpy(cache[i].response, response, sizeof(cache[i].response) - 1);
			cache[i].time_stored = time(NULL);
			return;
		}
	}
	strncpy(cache[cache_count].url, url, sizeof(cache[cache_count].url) - 1);
	strncpy(cache[cache_count].response, response, sizeof(cache[cache_count].response) - 1);
	cache[cache_count].time_stored = time(NULL);
	cache_count++;
}
int main(){
	cache_put("http://a.com/", "hello");
	cache_put("http://b.com/", "world");
	cache_put("http://a.com/", "new text");

	for (int i=0; i<cache_count;i++) {
		printf("%s %s %ld\n", cache[i].url, cache[i].response, (long)cache[i].time_stored);
	}
	return 0;
}
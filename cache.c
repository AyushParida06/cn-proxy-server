// cache module - Aarushi

#include<stdio.h>
#include<string.h>
#include<time.h>
#include<unistd.h>
#include "common.h"

struct CacheEntry {
    char url[512];        // the key: which page this is
    char response[8192];  // the saved reply
    time_t time_stored;   // when we saved it (used later for the TTL check)
};

// the cache: room for 10 entries, and a counter of how many are used
struct CacheEntry cache[10];
int cache_count = 0;

void cache_put(const char *url, const char *response) {
	for (int i = 0; i < cache_count; i++) {
    		if (strcmp(cache[i].url, url) == 0) {
        		strncpy(cache[i].response, response, sizeof(cache[i].response) - 1);
       	 		cache[i].time_stored = time(NULL);
        		return;
    		}
	}
	if (cache_count == 10) {
		int oldest = 0;
		for (int i = 1; i < cache_count; i++) {        
       	 		if (cache[i].time_stored < cache[oldest].time_stored) {  
            		oldest = i;                
        		}
		}

		strncpy(cache[oldest].url, url, sizeof(cache[oldest].url) - 1);
		strncpy(cache[oldest].response, response, sizeof(cache[oldest].response) - 1);
		cache[oldest].time_stored = time(NULL);

		return;
	}
	strncpy(cache[cache_count].url, url, sizeof(cache[cache_count].url) - 1);
	strncpy(cache[cache_count].response, response, sizeof(cache[cache_count].response) - 1);
	cache[cache_count].time_stored = time(NULL);
	cache_count++;
}

#define CACHE_TTL_SECONDS 3 

int cache_get(const char *url, char *response, int size) {
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].url, url) == 0) {
        	int age = time(NULL) - cache[i].time_stored;
		if (age < CACHE_TTL_SECONDS) {
			strncpy(response, cache[i].response, size - 1);
			response[size - 1] = '\0';
			return 1;
		}
        }
    }
    return 0;  
}

int main() {
    char buf[8192];

    // 1. nothing stored yet, so this should be a MISS
    if (cache_get("http://a.com/", buf, sizeof(buf))) {
        printf("HIT\n");
    } else {
        printf("MISS\n");
    }

    // 2. store a page
    cache_put("http://a.com/", "hello");

    // 3. now it should be a HIT, and buf should hold the saved page
    if (cache_get("http://a.com/", buf, sizeof(buf))) {
        printf("HIT\n");
        printf("%s\n", buf);
    } else {
        printf("MISS\n");
    }

    // 4. wait longer than the TTL (3 seconds), then it should be a MISS again
    sleep(4);
    if (cache_get("http://a.com/", buf, sizeof(buf))) {
        printf("HIT\n");
    } else {
        printf("MISS\n");
    }

    return 0;
}
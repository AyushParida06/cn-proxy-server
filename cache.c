// cache module - Aarushi

#include<stdio.h>
#include<string.h>
#include<time.h>
#include<unistd.h>
#include "common.h"
#include <pthread.h>

struct CacheEntry {
    int len;
    char url[512];        // the key: which page this is
    char response[8192];  // the saved reply
    time_t time_stored;   // when we saved it (used later for the TTL check)
};

// the cache: room for 10 entries, and a counter of how many are used
struct CacheEntry cache[10];
int cache_count = 0;
pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

void cache_put(const char *url, const char *response, int len) {
	if(len<=0 || len>sizeof(cache[0].response)) {
		return;
	}
	pthread_mutex_lock(&cache_lock);
	for (int i = 0; i < cache_count; i++) {
    		if (strcmp(cache[i].url, url) == 0) {
			memcpy(cache[i].response, response, len);
       	 		cache[i].time_stored = time(NULL);
			cache[i].len = len;           
			pthread_mutex_unlock(&cache_lock);
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
		memcpy(cache[oldest].response, response, len);
		cache[oldest].time_stored = time(NULL);          
		cache[oldest].len = len;       
		pthread_mutex_unlock(&cache_lock);
		return;
	}
	strncpy(cache[cache_count].url, url, sizeof(cache[cache_count].url) - 1);
	memcpy(cache[cache_count].response, response, len);
	cache[cache_count].time_stored = time(NULL);
	cache[cache_count].len = len;
	cache_count++;
	pthread_mutex_unlock(&cache_lock);
}

#define CACHE_TTL_SECONDS 60

int cache_get(const char *url, char *response, int size) {
    pthread_mutex_lock(&cache_lock);
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].url, url) == 0) {
        	int age = time(NULL) - cache[i].time_stored;
			if (age < CACHE_TTL_SECONDS) {
    				if ( cache[i].len > size ) {
        				pthread_mutex_unlock(&cache_lock);
        				return 0;
    				}
				memcpy(response, cache[i].response, cache[i].len);

  				pthread_mutex_unlock(&cache_lock);
    				return cache[i].len;
			}
        }
    }
    pthread_mutex_unlock(&cache_lock);
    return 0;  
}
#ifdef TEST
int main() {
    char buf[8192];

    // 1. nothing stored yet, so this should be a MISS
    if (cache_get("http://a.com/", buf, sizeof(buf))) {
        printf("HIT\n");
    } else {
        printf("MISS\n");
    }

    // 2. store a page
    cache_put("http://a.com/", "hello",5);

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
#endif
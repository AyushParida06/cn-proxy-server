// access control - Jaysmita

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "common.h"
#include <pthread.h>

#define RATE_LIMIT 20   

struct Client {
    char ip[16];
    int count;            // requests in the current minute
    time_t window_start;  // when this minute began
};
struct Client clients[50];
int client_count = 0;

pthread_mutex_t rate_lock = PTHREAD_MUTEX_INITIALIZER;

int is_rate_limited(const char *client_ip) {
    
    pthread_mutex_lock(&rate_lock);
    // 1. search: is this client already in the table?
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].ip, client_ip) == 0) {
            // known client: has the minute passed?
            if (time(NULL) - clients[i].window_start >= 60) {
                clients[i].count = 1;
                clients[i].window_start = time(NULL);
		pthread_mutex_unlock(&rate_lock);
                return 0;
            }
            clients[i].count++;
            if (clients[i].count > RATE_LIMIT) {
		pthread_mutex_unlock(&rate_lock);
                return 1;
            }
	    pthread_mutex_unlock(&rate_lock);
            return 0;
        }
    }

    // 2. not found: she decides what to do if the table is full (50 clients)
    
    if (client_count >= 50) {
	// table is full: let the request through without tracking this client,
	// so the proxy never writes past the end of the array
	pthread_mutex_unlock(&rate_lock);
	return 0;
    }

    // 3. add a new row for this client
    strncpy(clients[client_count].ip, client_ip, 15);
    clients[client_count].count = 1;
    clients[client_count].window_start = time(NULL);
    client_count++;
    pthread_mutex_unlock(&rate_lock);
    return 0;
}

int is_blocked(const char *host) {
    FILE *fp = fopen("blacklist.txt", "r");
    if (fp == NULL) {
        return 0;   // no readable list blocks nothing
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, host) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

#ifdef TEST
int main() {
    // test 1: the blacklist
    printf("facebook.com -> %d\n", is_blocked("facebook.com"));
    printf("example.com  -> %d\n", is_blocked("example.com"));

    // test 2: the rate limiter (limit is 5 while testing)
    for (int n = 1; n <= 7; n++) {
        printf("request %d -> %d\n", n, is_rate_limited("1.2.3.4"));
    }
    return 0;
}
#endif
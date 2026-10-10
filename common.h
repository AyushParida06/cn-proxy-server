#ifndef COMMON_H
#define COMMON_H

// cache.c (Aarushi)
int cache_get(const char *url, char *response, int size);
void cache_put(const char *url, const char *response);

// access_control.c (Jaysmita)
int is_blocked(const char *host);
int is_rate_limited(const char *client_ip);

#endif
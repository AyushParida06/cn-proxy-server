// proxy core - Ayush

#include <sys/select.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include "common.h"

#define PORT 8888

void handle_connect(int client_fd, char *target){
    int port = 443;                      

    char *colon = strchr(target, ':');
    if (colon != NULL) {
    	*colon = '\0';
   	 port = atoi(colon + 1);
    }
    struct hostent *server = gethostbyname(target);
    if (server == NULL) {
        char *bad_gateway = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
        write(client_fd, bad_gateway, strlen(bad_gateway));
        close(client_fd);
        return;
    }

    // second socket, to the real website
    int remote_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in remote;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    memcpy(&remote.sin_addr, server->h_addr_list[0], server->h_length);
    if (connect(remote_fd, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        printf("Connect failed\n");
        char *bad_gateway = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
        write(client_fd, bad_gateway, strlen(bad_gateway));
        close(remote_fd);
        close(client_fd);
        return;
    }
    char *established = "HTTP/1.1 200 Connection Established\r\n\r\n";
    write(client_fd, established, strlen(established));
    printf("connected to %s port %d\n", target, port);
    while (1) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(client_fd, &set);
        FD_SET(remote_fd, &set);
        int max = client_fd;
        if (remote_fd > max) { max = remote_fd; }
        select(max + 1, &set, NULL, NULL, NULL);
	char tmp[4096];
	int r;
	if (FD_ISSET(client_fd, &set)) {
	    r = read(client_fd, tmp, sizeof(tmp));
	    if (r <= 0){
	    	break;
	    }
	    write(remote_fd, tmp, r);
	}
	if (FD_ISSET(remote_fd, &set)) {
	    r = read(remote_fd, tmp, sizeof(tmp));
	    if (r <= 0){
	    	break;
	    }
	    write(client_fd, tmp, r);
	}

    }
    close(remote_fd);
    close(client_fd);
}

struct client_info {
    int fd;
    char ip[16];
};

// runs in its own thread: handles exactly one client
void *handle_client(void *arg) {
    // get our socket from the box main() made, then free the box
    struct client_info *info = (struct client_info *)arg;
    int client_fd = info->fd;
    char client_ip[16];
    strcpy(client_ip, info->ip);
    free(arg);

    // read the request into buf and end it with '\0' so it is a valid string
    char buf[2048];
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';

        // split the first line into method, url and version
        char method[16], url[512], version[16];
        int parsed = sscanf(buf, "%15s %511s %15s", method, url, version);
	char *bad_request = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";

	if (parsed < 3) {
            write(client_fd, bad_request, strlen(bad_request));
            close(client_fd);
            return NULL;
        }
	
	if (strcmp(method, "CONNECT") == 0){
	    handle_connect(client_fd, url);
	    return NULL;
	}
	if (strncmp(url, "http://", 7) != 0) {
            write(client_fd, bad_request, strlen(bad_request));
            close(client_fd);
            return NULL;
        }

        printf("method: %s\n", method);
	printf("client: %s\n", client_ip);
        printf("url:    %s\n", url);

        // skip "http://" (7 characters), then copy the host name
        // until we reach '/', ':' or the end of the string
        char host[256];
        char *start = url + 7;
        int i = 0;
        while (start[i] != '/' && start[i] != ':' && start[i] != '\0' && i < 255) {
            host[i] = start[i];
            i++;
        }
        host[i] = '\0';
        printf("host:   %s\n", host);

	char cached[8192];
	if(strcmp(method, "GET") == 0 && cache_get(url, cached, sizeof(cached))){
	    write(client_fd, cached, strlen(cached));
	    printf("[HIT] %s %s\n", client_ip, url);
	    close(client_fd);return NULL;
	}

        // DNS lookup; if it fails, tell the client with a 502
        struct hostent *server = gethostbyname(host);
        if (server == NULL) {
            char *bad_gateway = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, bad_gateway, strlen(bad_gateway));
            close(client_fd);
            return NULL;
        }

        // second socket, to the real website
        int remote_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in remote;
        memset(&remote, 0, sizeof(remote));
        remote.sin_family = AF_INET;
        remote.sin_port = htons(80);
        memcpy(&remote.sin_addr, server->h_addr_list[0], server->h_length);
        if (connect(remote_fd, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
            printf("Connect failed\n");
            char *bad_gateway = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, bad_gateway, strlen(bad_gateway));
            close(remote_fd);
            close(client_fd);
            return NULL;
        }
        printf("connected to %s\n", host);

        // build a clean request with only the path, and send it
        char *path = strchr(url + 7, '/');
        if (path == NULL) {
            path = "/";
        }
        char req[1024];
        snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
        printf("Sending:\n%s", req);
        write(remote_fd, req, strlen(req));

        // relay the website's reply to the client
        char tmp[4096];
        int r;
        while ((r = read(remote_fd, tmp, sizeof(tmp))) > 0) {
            write(client_fd, tmp, r);
        }
        close(remote_fd);
	time_t now = time(NULL);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

	printf("[%s] %s %s ALLOWED\n", stamp, client_ip, url);

        printf("Received:\n%s\n", buf);
    }

    // done with this client
    close(client_fd);
    return NULL;
}

int main() {
    // create a TCP socket (the "shop")
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // describe the address: IPv4, any interface, port 8888
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    // attach the socket to the port, then start waiting
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Proxy listening on port %d...\n", PORT);

    // keep accepting clients; each one gets its own thread
    while (1) {
	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
	char *ip = inet_ntoa(client_addr.sin_addr);

        // a separate box per client, so the thread has its own copy
        struct client_info *p = malloc(sizeof(struct client_info));
        p->fd = client_fd;
	strcpy(p->ip, ip);

        pthread_t t;
        pthread_create(&t, NULL, handle_client, p);
        pthread_detach(t);
    }

    close(server_fd);
    return 0;
}
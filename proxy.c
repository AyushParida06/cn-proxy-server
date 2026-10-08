// proxy core - Ayush

// proxy core - Ayush

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8888

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

    // keep serving clients one after another
    while (1) {
        // wait for a client; client_fd is the private socket for this client
        int client_fd = accept(server_fd, NULL, NULL);

        // read the request into buf and end it with '\0' so it is a valid string
        char buf[2048];
        int n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';

            // split the first line into method, url and version
            // (the numbers limit how many characters are read, so long input cannot overflow)
            char method[16], url[512], version[16];
            sscanf(buf, "%15s %511s %15s", method, url, version);
            printf("method: %s\n", method);
            printf("url:    %s\n", url);

            // NEW: skip "http://" (7 characters), then copy the host name
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

            printf("Received:\n%s\n", buf);
        }

        // send a fixed HTTP reply (body is 16 bytes including the newline)
        char *reply = "HTTP/1.1 200 OK\r\nContent-Length: 16\r\n\r\nproxy is alive!\n";
        write(client_fd, reply, strlen(reply));

        // done with this client; the server socket stays open for the next one
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
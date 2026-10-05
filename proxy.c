// proxy core - Ayush

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8888

int main() {
    // ---------- Layer 1 (already there) ----------
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Proxy listening on port %d...\n", PORT);

    // ---------- Layer 2 (paste your new code here) ----------
    int client_fd = accept(server_fd, NULL, NULL);

    char buf[2048];
    int n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("Received:\n%s\n", buf);
    }

    char *reply = "HTTP/1.1 200 OK\r\nContent-Length: 16\r\n\r\nproxy is alive!\n";
    write(client_fd, reply, strlen(reply));

    close(client_fd);
    close(server_fd);

    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include "parse.h"

#define ECHO_PORT 9999
#define BUF_SIZE 4096

int sock = -1, client_sock = -1;
char buf[BUF_SIZE];
char tmp1[BUF_SIZE] = "HTTP/1.1 400 Bad request\r\n\r\n";
char tmp2[BUF_SIZE] = "HTTP/1.1 501 Not Implemented\r\n\r\n";

int close_socket(int sock) {
    if (close(sock)) {
        fprintf(stderr, "Failed closing socket.\n");
        return 1;
    }
    return 0;
}

void handle_signal(int sig) {
    if (sock != -1) {
        fprintf(stderr, "\nReceived signal %d. Closing socket.\n", sig);
        close_socket(sock);
    }
    exit(0);
}

void handle_sigpipe(int sig) {
    if (sock != -1) return;
    exit(0);
}

int main(int argc, char *argv[]) {
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGSEGV, handle_signal);
    signal(SIGABRT, handle_signal);
    signal(SIGQUIT, handle_signal);
    signal(SIGTSTP, handle_signal);
    signal(SIGFPE, handle_signal);
    signal(SIGHUP, handle_signal);
    signal(SIGPIPE, handle_sigpipe);

    struct sockaddr_in addr, cli_addr;
    socklen_t cli_size;
    fprintf(stdout, "----- HTTP Echo Server -----\n");

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Failed creating socket.\n");
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        fprintf(stderr, "Failed setting socket options.\n");
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(ECHO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr))) {
        close_socket(sock);
        fprintf(stderr, "Failed binding socket.\n");
        return EXIT_FAILURE;
    }

    if (listen(sock, 5)) {
        close_socket(sock);
        fprintf(stderr, "Error listening on socket.\n");
        return EXIT_FAILURE;
    }

    while (1) {
        cli_size = sizeof(cli_addr);
        printf("Waiting for connection...\n");
        client_sock = accept(sock, (struct sockaddr*)&cli_addr, &cli_size);
        if (client_sock == -1) {
            fprintf(stderr, "Error accepting connection.\n");
            close_socket(sock);
            return EXIT_FAILURE;
        }
        printf("New connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        while (1) {
            memset(buf, 0, BUF_SIZE);
            ssize_t readret = recv(client_sock, buf, BUF_SIZE, 0);
            if (readret <= 0) break;

            Request *req = parse(buf, readret, client_sock);
            if (!req) {
                send(client_sock, tmp1, strlen(tmp1), 0);
            } else if (strcmp(req->http_method, "GET") == 0 ||
                       strcmp(req->http_method, "POST") == 0 ||
                       strcmp(req->http_method, "HEAD") == 0) {
                send(client_sock, buf, readret, 0);
                free(req->headers);
                free(req);
            } else {
                send(client_sock, tmp2, strlen(tmp2), 0);
                free(req->headers);
                free(req);
            }
        }

        close_socket(client_sock);
        printf("Closed connection from %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    }

    close_socket(sock);
    return EXIT_SUCCESS;
}

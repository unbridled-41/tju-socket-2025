#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <string.h>
#include "parse.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <asm-generic/socket.h>

#define ECHO_PORT 9999
#define BUF_SIZE 4096
#define URL_MAX_SIZE 256

int sock = -1, client_sock = -1;
char buf[BUF_SIZE];

// HTTP响应消息
char RESPONSE_400[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
char RESPONSE_404[] = "HTTP/1.1 404 Not Found\r\n\r\n";
char RESPONSE_501[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
char RESPONSE_505[] = "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n";
char RESPONSE_200[] = "HTTP/1.1 200 OK\r\n\r\n";

// 配置参数
char http_version_now[] = "HTTP/1.1";
char root_path[] = "./static_site";
char file_path[] = "/index.html";

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

// 发送数据并检查完整性
int send_all(int socket, const char *data, size_t length) {
    ssize_t bytes_sent;
    size_t total = 0;
    while (total < length) {
        bytes_sent = send(socket, data + total, length - total, 0);
        if (bytes_sent <= 0) return 0; // 发送失败
        total += bytes_sent;
    }
    return 1;
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
    fprintf(stdout, "----- HTTP Server -----\n");

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
                if (!send_all(client_sock, RESPONSE_400, strlen(RESPONSE_400))) {
                    fprintf(stderr, "Failed to send 400 response\n");
                    close_socket(client_sock);
                    close_socket(sock);
                    return EXIT_FAILURE;
                }
            } else {
                if (strcmp(req->http_version, http_version_now) != 0) {
                    if (!send_all(client_sock, RESPONSE_505, strlen(RESPONSE_505))) {
                        fprintf(stderr, "Failed to send 505 response\n");
                        free(req->headers);
                        free(req);
                        close_socket(client_sock);
                        close_socket(sock);
                        return EXIT_FAILURE;
                    }
                } else if (strcmp(req->http_method, "POST") == 0) {
                    if (!send_all(client_sock, buf, readret)) {
                        fprintf(stderr, "Failed to echo POST data\n");
                        free(req->headers);
                        free(req);
                        close_socket(client_sock);
                        close_socket(sock);
                        return EXIT_FAILURE;
                    }
                } else if (strcmp(req->http_method, "GET") == 0 || strcmp(req->http_method, "HEAD") == 0) {
                    char file_url[URL_MAX_SIZE];
                    snprintf(file_url, URL_MAX_SIZE, "%s%s", root_path, 
                             (strcmp(req->http_uri, "/") == 0) ? file_path : req->http_uri);

                    struct stat file_stat;
                    if (stat(file_url, &file_stat) == -1 || 
                        !S_ISREG(file_stat.st_mode) || 
                        !(file_stat.st_mode & S_IRUSR)) {
                        if (!send_all(client_sock, RESPONSE_404, strlen(RESPONSE_404))) {
                            fprintf(stderr, "Failed to send 404 response\n");
                            free(req->headers);
                            free(req);
                            close_socket(client_sock);
                            close_socket(sock);
                            return EXIT_FAILURE;
                        }
                    } else {
                        int fd = open(file_url, O_RDONLY);
                        if (fd < 0) {
                            if (!send_all(client_sock, RESPONSE_404, strlen(RESPONSE_404))) {
                                fprintf(stderr, "Failed to send 404 response\n");
                                free(req->headers);
                                free(req);
                                close_socket(client_sock);
                                close_socket(sock);
                                return EXIT_FAILURE;
                            }
                        } else {
                            char response[BUF_SIZE];
                            memset(response, 0, BUF_SIZE);
                            strcpy(response, RESPONSE_200);
                            ssize_t bytes_read = read(fd, response + strlen(RESPONSE_200), BUF_SIZE - strlen(RESPONSE_200) - 1);
                            close(fd);
                            if (bytes_read <= 0) {
                                if (!send_all(client_sock, RESPONSE_404, strlen(RESPONSE_404))) {
                                    fprintf(stderr, "Failed to send 404 response\n");
                                    free(req->headers);
                                    free(req);
                                    close_socket(client_sock);
                                    close_socket(sock);
                                    return EXIT_FAILURE;
                                }
                            } else {
                                // HEAD请求不发送正文
                                size_t response_len = (strcmp(req->http_method, "HEAD") == 0) 
                                    ? strlen(RESPONSE_200) 
                                    : strlen(RESPONSE_200) + bytes_read;
                                if (!send_all(client_sock, response, response_len)) {
                                    fprintf(stderr, "Failed to send file content\n");
                                    free(req->headers);
                                    free(req);
                                    close_socket(client_sock);
                                    close_socket(sock);
                                    return EXIT_FAILURE;
                                }
                            }
                        }
                    }
                } else {
                    if (!send_all(client_sock, RESPONSE_501, strlen(RESPONSE_501))) {
                        fprintf(stderr, "Failed to send 501 response\n");
                        free(req->headers);
                        free(req);
                        close_socket(client_sock);
                        close_socket(sock);
                        return EXIT_FAILURE;
                    }
                }
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

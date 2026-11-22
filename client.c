#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_MESSAGE 65536u

static int send_all(int fd, const void *data, size_t length) {
    const unsigned char *p = data;
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return -1;
        }
        if (n == 0) { fprintf(stderr, "Сервер закрыл соединение при отправке\n"); return -1; }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *data, size_t length) {
    unsigned char *p = data;
    size_t received = 0;
    while (received < length) {
        ssize_t n = recv(fd, p + received, length - received, 0);
        if (n == 0) {
            fprintf(stderr, received == 0 ? "Сервер закрыл соединение без ответа\n" : "Сервер оборвал ответ\n");
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return -1;
        }
        received += (size_t)n;
    }
    return 0;
}

static int exchange(int fd, const char *request) {
    size_t length = strlen(request);
    if (length == 0 || length > MAX_MESSAGE) {
        fprintf(stderr, "Недопустимая длина запроса\n");
        return -1;
    }
    uint32_t request_size = htonl((uint32_t)length);
    if (send_all(fd, &request_size, sizeof request_size) < 0 || send_all(fd, request, length) < 0) return -1;
    uint32_t response_size;
    if (recv_all(fd, &response_size, sizeof response_size) < 0) return -1;
    uint32_t response_length = ntohl(response_size);
    if (response_length == 0 || response_length > MAX_MESSAGE) {
        fprintf(stderr, "Некорректная длина ответа\n");
        return -1;
    }
    char *response = malloc((size_t)response_length + 1);
    if (!response) { perror("malloc"); return -1; }
    if (recv_all(fd, response, response_length) < 0) { free(response); return -1; }
    response[response_length] = '\0';
    printf("%s -> %s\n", request, response);
    free(response);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Использование: %s '1+2' ['3*4' ...]\n", argv[0]);
        return 1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        fprintf(stderr, "Некорректный адрес\n"); close(fd); return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect"); close(fd); return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (exchange(fd, argv[i]) < 0) { close(fd); return 1; }
    }
    if (close(fd) < 0) { perror("close"); return 1; }
    return 0;
}

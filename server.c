#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_MESSAGE 65536u

typedef struct {
    const char *p;
    int invalid;
} Parser;

static void skip_space(Parser *s) {
    while (isspace((unsigned char)*s->p)) s->p++;
}

static double expr(Parser *s);

static double factor(Parser *s) {
    skip_space(s);
    if (*s->p == '+' || *s->p == '-') {
        int negative = *s->p++ == '-';
        double value = factor(s);
        return negative ? -value : value;
    }
    if (*s->p == '(') {
        s->p++;
        double value = expr(s);
        skip_space(s);
        if (*s->p != ')') s->invalid = 1;
        else s->p++;
        return value;
    }
    char *end;
    errno = 0;
    double value = strtod(s->p, &end);
    if (end == s->p || errno == ERANGE || !isfinite(value)) s->invalid = 1;
    if (end != s->p) s->p = end;
    return value;
}

static double term(Parser *s) {
    double value = factor(s);
    for (;;) {
        skip_space(s);
        if (*s->p != '*' && *s->p != '/') return value;
        char operation = *s->p++;
        double other = factor(s);
        if (operation == '/' && other == 0) s->invalid = 2;
        if (s->invalid) return 0;
        value = operation == '*' ? value * other : value / other;
        if (!isfinite(value)) s->invalid = 1;
    }
}

static double expr(Parser *s) {
    double value = term(s);
    for (;;) {
        skip_space(s);
        if (*s->p != '+' && *s->p != '-') return value;
        char operation = *s->p++;
        double other = term(s);
        if (s->invalid) return 0;
        value = operation == '+' ? value + other : value - other;
        if (!isfinite(value)) s->invalid = 1;
    }
}

static int recv_all(int fd, void *data, size_t length) {
    unsigned char *p = data;
    size_t received = 0;
    while (received < length) {
        ssize_t n = recv(fd, p + received, length - received, 0);
        if (n == 0) return received == 0 ? 0 : -2;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        received += (size_t)n;
    }
    return 1;
}

static int send_all(int fd, const void *data, size_t length) {
    const unsigned char *p = data;
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int send_message(int fd, const char *message) {
    size_t length = strlen(message);
    if (length > MAX_MESSAGE) return -1;
    uint32_t size = htonl((uint32_t)length);
    if (send_all(fd, &size, sizeof size) < 0) return -1;
    return send_all(fd, message, length);
}

static int handle_client(int fd) {
    for (;;) {
        uint32_t network_size;
        int status = recv_all(fd, &network_size, sizeof network_size);
        if (status == 0) return 0;
        if (status != 1) return -1;
        uint32_t length = ntohl(network_size);
        if (length == 0 || length > MAX_MESSAGE) {
            send_message(fd, "ERR:invalid length");
            return -1;
        }
        char *message = malloc((size_t)length + 1);
        if (!message) return -1;
        status = recv_all(fd, message, length);
        if (status != 1) {
            free(message);
            return -1;
        }
        message[length] = '\0';
        char answer[128];
        if (memchr(message, '\0', length)) {
            snprintf(answer, sizeof answer, "ERR:invalid expression");
        } else {
            Parser parser = {message, 0};
            double result = expr(&parser);
            skip_space(&parser);
            if (*parser.p != '\0') parser.invalid = 1;
            if (parser.invalid == 2) snprintf(answer, sizeof answer, "ERR:division by zero");
            else if (parser.invalid || !isfinite(result)) snprintf(answer, sizeof answer, "ERR:invalid expression");
            else snprintf(answer, sizeof answer, "OK:%.15g", result);
        }
        printf("Запрос: %s | Ответ: %s\n", message, answer);
        fflush(stdout);
        free(message);
        if (send_message(fd, answer) < 0) return -1;
    }
}

int main(void) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int yes = 1;
    if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("setsockopt"); close(server); return 1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(server); return 1;
    }
    if (listen(server, 16) < 0) { perror("listen"); close(server); return 1; }
    puts("Сервер слушает 127.0.0.1:9000");
    fflush(stdout);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); close(server); return 1;
        }
        if (handle_client(client) < 0) fprintf(stderr, "Соединение завершилось с ошибкой\n");
        close(client);
    }
}

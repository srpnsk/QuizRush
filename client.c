#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <netdb.h>

#define SERVER_PORT 5000
#define MAX_NAME_LEN 50
#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Использование: %s <IP или hostname>\n", argv[0]);
        return 1;
    }

    char name[MAX_NAME_LEN];
    char buffer[BUFFER_SIZE];

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    int sock = -1;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", SERVER_PORT);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(argv[1], port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1)
        continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
        break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock == -1) {
        perror("connect");
        return -1;
    }


    printf("Введите ваше имя: ");
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = 0;

    // Отправляем имя серверу
    send(sock, name, strlen(name), 0);

    printf("Подключение установлено! Ожидаем вопросы...\n");

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, 2, -1); // ждем события
        if (ret < 0) {
            perror("poll");
            break;
        }

        // Сервер прислал сообщение
        if (fds[1].revents & POLLIN) {
            int n = recv(sock, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) {
                printf("\nСервер закрыл соединение.\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
        }

        // Пользователь ввел ответ
        if (fds[0].revents & POLLIN) {
            char input[16];
            if (fgets(input, sizeof(input), stdin) != NULL) {
                input[strcspn(input, "\n")] = 0;
                send(sock, input, strlen(input), 0);
            }
        }
    }

    close(sock);
    printf("Соединение закрыто.\n");
    return 0;
}

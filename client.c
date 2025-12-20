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

    char *host = argv[1];
    int sock;
    struct sockaddr_in server_addr;
    char name[MAX_NAME_LEN];
    char buffer[BUFFER_SIZE];

    // Создаем сокет
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Пробуем распознать как IP
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        // Если не IP, пытаемся как hostname
        struct hostent *he = gethostbyname(host);
        if (!he) {
            fprintf(stderr, "Неизвестный хост: %s\n", host);
            exit(1);
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    // Подключаемся к серверу
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
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

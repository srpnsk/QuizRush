#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>

#define SERVER_PORT 5000
#define MAX_NAME_LEN 50
#define BUFFER_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Использование: %s <IP или hostname>\n", argv[0]);
        return 1;
    }

    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char name[MAX_NAME_LEN];
    int name_sent = 0;   // ← ВАЖНО

    /* === socket === */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(argv[1]);
        if (!he) {
            fprintf(stderr, "Неизвестный хост\n");
            exit(1);
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("Введите имя (сервер не блокируется):\n");

    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    while (1) {
        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            break;
        }

        /* === сервер всегда читаем === */
        if (fds[1].revents & POLLIN) {
            int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                printf("\nСервер закрыл соединение\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        /* === ввод пользователя === */
        if (fds[0].revents & POLLIN) {

            if (!name_sent) {
                if (fgets(name, sizeof(name), stdin)) {
                    name[strcspn(name, "\n")] = 0;

                    if (strlen(name) == 0)
                        continue;

                    send(sock, name, strlen(name), 0);
                    send(sock, "\n", 1, 0);

                    name_sent = 1;
                    printf("Имя отправлено. Ждём игру...\n");
                }
            } 
            else {
                char input[64];
                if (fgets(input, sizeof(input), stdin)) {
                    input[strcspn(input, "\n")] = 0;
                    send(sock, input, strlen(input), 0);
                    send(sock, "\n", 1, 0);
                }
            }
        }
    }

    close(sock);
    return 0;
}

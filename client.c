#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT 5000
#define BUFFER_SIZE 256

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Использование: %s <IP или hostname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *host = argv[1];
    char player_name[32];
    printf("Введите имя игрока: ");
    fgets(player_name, sizeof(player_name), stdin);
    player_name[strcspn(player_name, "\n")] = 0; // убираем \n

    // Создание сокета
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Преобразуем IP или разрешаем hostname
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            fprintf(stderr, "Неизвестный хост: %s\n", host);
            exit(EXIT_FAILURE);
        }
        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    // Подключаемся к серверу
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Ошибка подключения");
        exit(EXIT_FAILURE);
    }

    // Отправляем имя игрока серверу сразу после подключения
    send(sock, player_name, strlen(player_name), 0);

    char buffer[BUFFER_SIZE];
    while (1) {
        int n = recv(sock, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        printf("%s", buffer);

        // Если пришёл вопрос, ждём ответ игрока
        if (strchr(buffer, '?')) {
            printf("Ваш ответ: ");
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            send(sock, buffer, strlen(buffer), 0);
        }
    }

    close(sock);
    return 0;
}

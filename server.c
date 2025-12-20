#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

#define PORT 5000
#define MAX_CLIENTS 2
#define BUFFER_SIZE 256

typedef struct {
    char question[256];
    char answer[32];
} Quiz;

Quiz quiz[] = {
    {"2+2=?\n", "4"},
    {"Столица Франции?\n", "Париж"},
    {"Цвет неба?\n", "синий"}
};

void print_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    getifaddrs(&ifaddr);

    printf("Локальные IP для подключения:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            if (strcmp(ip, "127.0.0.1") != 0)
                printf("  %s\n", ip);
        }
    }
    freeifaddrs(ifaddr);
}

int main() {
    int server_fd, clients[MAX_CLIENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int scores[MAX_CLIENTS] = {0};

    char buffer1[BUFFER_SIZE], buffer2[BUFFER_SIZE], results[BUFFER_SIZE];

    // Создаем сокет
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); exit(1);
    }

    // Печатаем hostname и IP
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints = {0}, *res;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            printf("Подключайтесь через hostname: %s или IP: %s\n", hostname, ip);
            freeaddrinfo(res);
        } else {
            printf("Не удалось разрешить hostname. ");
            print_local_ip();
        }
    } else {
        printf("Не удалось получить hostname. ");
        print_local_ip();
    }

    printf("Ожидаем %d игроков...\n", MAX_CLIENTS);

    // Принимаем клиентов
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (clients[i] < 0) { perror("accept"); exit(1); }
        printf("Игрок %d подключился!\n", i + 1);
        char msg[BUFFER_SIZE];
        snprintf(msg, sizeof(msg), "Добро пожаловать, Игрок %d!\n", i + 1);
        send(clients[i], msg, strlen(msg), 0);
    }

    int quiz_count = sizeof(quiz) / sizeof(quiz[0]);

    for (int i = 0; i < quiz_count; i++) {
        // Отправка вопроса
        for (int j = 0; j < MAX_CLIENTS; j++)
            send(clients[j], quiz[i].question, strlen(quiz[i].question), 0);

        // Чтение ответов
        int n1 = recv(clients[0], buffer1, sizeof(buffer1)-1, 0);
        buffer1[n1] = '\0';
        int n2 = recv(clients[1], buffer2, sizeof(buffer2)-1, 0);
        buffer2[n2] = '\0';

        // Проверка
        if (strncasecmp(buffer1, quiz[i].answer, strlen(quiz[i].answer)) == 0)
            scores[0]++;
        if (strncasecmp(buffer2, quiz[i].answer, strlen(quiz[i].answer)) == 0)
            scores[1]++;

        // Отправка результатов
        snprintf(results, sizeof(results),
                 "\nРезультаты после вопроса %d:\nИгрок 1: %d\nИгрок 2: %d\n\n",
                 i+1, scores[0], scores[1]);
        for (int j = 0; j < MAX_CLIENTS; j++)
            send(clients[j], results, strlen(results), 0);
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
        close(clients[i]);
    close(server_fd);
    return 0;
}

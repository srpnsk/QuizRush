#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

#define PORT 5000
#define MAX_PLAYERS 2
#define BUFFER_SIZE 256

typedef struct {
    char question[256];
    char answer[32];
} Quiz;


typedef struct Player{
    int id;                // уникальный идентификатор
    int sock;              // сокет игрока
    char name[32];         // имя игрока
    int score;             // очки
    struct Player *next;   // следующий игрок в списке
} Player;

// Добавление нового игрока в список
Player* add_player(Player *head, int sock, const char *name, int id) {
    Player *p = malloc(sizeof(Player));
    if (!p) { perror("malloc"); exit(1); }
    p->id = id;
    p->sock = sock;
    strncpy(p->name, name, sizeof(p->name)-1);
    p->name[sizeof(p->name)-1] = '\0';
    p->score = 0;
    p->next = NULL;

    if (!head) return p;

    Player *cur = head;
    while (cur->next) cur = cur->next;
    cur->next = p;
    return head;
}

// Отправка сообщения всем игрокам, кроме указанных
void send_to_all_except(Player *head, const char *msg, int exclude_id) {
    Player *cur = head;
    while (cur) {
        if (cur->id != exclude_id) {
            send(cur->sock, msg, strlen(msg), 0);
        }
        cur = cur->next;
    }
}

// Освобождение памяти списка
void free_players(Player *head) {
    Player *cur = head;
    while (cur) {
        Player *tmp = cur;
        cur = cur->next;
        close(tmp->sock);
        free(tmp);
    }
}

Quiz quiz[] = {
    {"What is the sum of 2 and 2?\n", "4"},
    {"What is the capital city of France?\n", "Paris"},
    {"What color is the sky on a clear day?\n", "blue"}
};

void print_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }
    printf("IP:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            if (strcmp(ip, "127.0.0.1") != 0) {printf("  %s\n", ip);}
            
        }
    }
    freeifaddrs(ifaddr);
}



int main() {
    int server_fd, clients[MAX_PLAYERS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    Player players[MAX_PLAYERS]; // Создаем список игроков
    int quiz_count = sizeof(quiz)/sizeof(quiz[0]); // Узнаем количество вопросов

    char buffer1[BUFFER_SIZE], buffer2[BUFFER_SIZE], results[BUFFER_SIZE];

    // Создаем сокет
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("listen"); exit(1);
    }

    // Печатаем hostname и IP
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        printf("Hostname: %s\n", hostname);
    } else {
        perror("gethostname");
    }
    print_local_ip();
    printf("0/%d connected...\n", MAX_PLAYERS);

    // Принимаем клиентов
    Player *head = NULL;
    int player_counter = 1;
    while (player_counter <= MAX_PLAYERS) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            perror("accept");
            continue; // ждём следующего клиента
        }

        // Читаем имя игрока
        char name[32];
        int n = recv(client_sock, name, sizeof(name)-1, 0);
        if (n <= 0) snprintf(name, sizeof(name), "Player%d", player_counter);
        else name[n] = '\0';

        // Добавляем игрока в список
        head = add_player(head, client_sock, name, player_counter);

        // Отправляем приветствие
        char welcome[64], connection[64], notify[64];
        snprintf(welcome, sizeof(welcome), "Welcome, %s!\n", name);
        snprintf(notify, sizeof(notify), "%s connected!\n", name);
        snprintf(connection, sizeof(connection), "%d/%d player(s) connected...\n", player_counter, MAX_PLAYERS);
        send(client_sock, welcome, strlen(welcome), 0);
        send_to_all_except(head, notify, player_counter);
        send_to_all_except(head, connection, -1);
        printf("%s", notify);
        printf("%s", connection);

        player_counter++;
    }
    send_to_all_except(head, "All players connected! Game starts!\n", -1);
    printf("All players connected! Game starts!\n");


    
    // Викторина

    Player *cur;
    for (int q = 0; q < quiz_count; q++) {
        // Отправляем вопрос всем
        send_to_all_except(head, quiz[q].question, -1);

        // Получаем ответы
        cur = head;
        while (cur) {
            char buffer[256];
            int n = recv(cur->sock, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) strcpy(buffer, "");
            else buffer[n] = '\0';

            if (strncasecmp(buffer, quiz[q].answer, strlen(quiz[q].answer)) == 0)
                cur->score++;

            cur = cur->next;
        }
        // Формируем результаты
        char results[512];
        snprintf(results, sizeof(results), "\nРезультаты после вопроса %d:\n", q+1);

        cur = head;
        while (cur) {
            char line[64];
            snprintf(line, sizeof(line), "%s: %d очков\n", cur->name, cur->score);
            strncat(results, line, sizeof(results)-strlen(results)-1);
            cur = cur->next;
        }

        // Отправляем результаты всем
        send_to_all_except(head, results, -1);
    }

    // Закрываем соединения
    for (int i = 0; i < MAX_PLAYERS; i++) close(players[i].sock);
    close(server_fd);

    printf("Игра окончена!\n");
    return 0;
}

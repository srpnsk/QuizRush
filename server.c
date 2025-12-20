#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>



#define PORT 5000
#define MAX_PLAYERS 10
#define MAX_QUESTION_LEN 256
#define MAX_ANSWER_LEN 100
#define MAX_NAME_LEN 50
#define OPTIONS_COUNT 4
#define TIME_PER_QUESTION 20
#define BASE_POINTS 10
#define CONNECT_TIMEOUT 30
#define QUESTIONS_FILE "questions.txt"

typedef struct {
    char question[MAX_QUESTION_LEN];
    char options[OPTIONS_COUNT][MAX_ANSWER_LEN];
    int correct_option;
} Question;


typedef struct Player{
    int id;                   // уникальный идентификатор
    int sock;                 // сокет игрока
    char name[MAX_NAME_LEN];  // имя игрока
    int score;                // очки
    int answered;             // ответил ли за раунд
    int answer;               // ответ
    int answer_time;          // время ответа
    struct Player *next;      // следующий игрок в списке
} Player;

// Массив временных данных игроков, которые ещё не прислали имя
typedef struct {
    int sock;
    char name[MAX_NAME_LEN];
    int bytes_received;
} PendingPlayer;

Player *head = NULL;   // глобальный список игроков
int server_fd = -1;    // глобальный серверный сокет
Question *questions = NULL;  // глобальные вопросы
int question_count = 0;

void send_to_all_except(Player *head, const char *msg, int exclude_id);
void free_players(Player *head);

// Добавление нового игрока в список
Player* add_player(Player *head, int sock, const char *name, int id) {
    Player *p = malloc(sizeof(Player));
    if (!p) { perror("malloc"); exit(1); }
    p->id = id;
    p->sock = sock;
    strncpy(p->name, name, sizeof(p->name)-1);
    p->name[sizeof(p->name)-1] = '\0';
    p->score = 0;
    p->answered = 0;
    p->next = NULL;

    if (!head) return p;

    Player *cur = head;
    while (cur->next) cur = cur->next;
    cur->next = p;
    return head;
}

Player* remove_player(Player *head, int sock) {
    Player *cur = head;
    Player *prev = NULL;

    while (cur) {
        if (cur->sock == sock) {
            if (prev) prev->next = cur->next;
            else head = cur->next;  // удаляем голову списка

            close(cur->sock);
            free(cur);
            return head;
        }
        prev = cur;
        cur = cur->next;
    }
    return head;  // если не нашли игрока, возвращаем список без изменений
}

void handle_sigint(int sig) {
    printf("\n⚠️  Сервером получен SIGINT, закрываем соединения...\n");

    // Отправляем сообщение всем игрокам
    if (head) {
        send_to_all_except(head, "\nСервер завершает работу. Игра остановлена.\n", -1);
    }

    // Закрываем всех игроков
    if (head) free_players(head);

    // Закрываем серверный сокет
    if (server_fd >= 0) close(server_fd);

    free(questions);
    exit(0);
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

void send_to_pending(PendingPlayer *pending, int count, const char *msg) {
    for (int i = 0; i < count; i++) {
        send(pending[i].sock, msg, strlen(msg), 0);
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

int load_questions(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Ошибка: не удалось открыть файл %s\n", filename);
        return 0;
    }
    
    char line[MAX_QUESTION_LEN * 2];
    int count = 0;
    
    // Считаем количество вопросов
    while (fgets(line, sizeof(line), file)) {
        if (strlen(line) > 1) count++;
    }
    question_count = count / 6;
    
    // Выделяем память
    questions = malloc(question_count * sizeof(Question));
    if (!questions) {
        fclose(file);
        return 0;
    }
    
    // Читаем вопросы
    rewind(file);
    for (int i = 0; i < question_count; i++) {
        // Вопрос
        fgets(questions[i].question, MAX_QUESTION_LEN, file);
        questions[i].question[strcspn(questions[i].question, "\n")] = 0;
        
        // Варианты ответов
        for (int j = 0; j < OPTIONS_COUNT; j++) {
            fgets(questions[i].options[j], MAX_ANSWER_LEN, file);
            questions[i].options[j][strcspn(questions[i].options[j], "\n")] = 0;
        }
        
        // Правильный ответ
        fgets(line, sizeof(line), file);
        sscanf(line, "%d", &questions[i].correct_option);
    }
    
    fclose(file);
    printf("Загружено %d вопросов\n", question_count);
    return 1;
}

void clean_string(char *str) {
    int i = 0, j = 0;
    while (str[i]) {
        if (str[i] != '\n' && str[i] != '\r') {
            str[j++] = str[i];
        }
        i++;
    }
    str[j] = '\0';
}

// Проверка, существует ли уже такое имя
int name_exists(Player *head, const char *name) {
    Player *cur = head;
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            return 1; // имя найдено
        }
        cur = cur->next;
    }
    return 0; // имя не найдено
}

// Подсчет очков с учетом скорости
int calculate_score(int is_correct, int time_spent) {
    if (!is_correct) return 0;
    
    // Базовые очки + бонус за скорость
    int time_bonus = TIME_PER_QUESTION - time_spent;
    if (time_bonus < 0) time_bonus = 0;
    
    return BASE_POINTS + time_bonus;
}

// Отправка вопроса игрокам
void send_question(Player* head, int q_index) {
    char buffer[1024];
    Question q = questions[q_index];
    
    snprintf(buffer, sizeof(buffer),
             "\n════════════════════════════════════════\n"
             "Вопрос %d/%d:\n"
             "%s\n\n"
             "Варианты ответов:\n"
             "1) %s\n"
             "2) %s\n"
             "3) %s\n"
             "4) %s\n\n"
             "У вас есть %d секунд! Введите номер ответа (1-4): \n",
             q_index + 1, question_count,
             q.question,
             q.options[0], q.options[1], q.options[2], q.options[3],
             TIME_PER_QUESTION);
    
    send_to_all_except(head, buffer, -1);
}

// Проверка, все ли игроки ответили
int all_players_answered(Player *head) {
    Player *cur = head;
    while (cur) {
        if (!cur->answered) {  // если игрок не ответил
            return 0;
        }
        cur = cur->next;
    }
    return 1; // все игроки ответили
}

// Сброс флагов ответов для нового раунда
void reset_round_flags(Player *head) {
    Player *cur = head;
    while (cur) {
        cur->answered = 0;
        cur->answer = 0;
        cur->answer_time = 0;
        cur = cur->next;
    }
}

// Обработка раунда с таймером
void process_round(Player *head, int q_index) {
    printf("\n📝 Вопрос %d/%d: %s\n", q_index + 1, question_count, questions[q_index].question);
    
    // Сбрасываем флаги
    reset_round_flags(head);
    
    // Отправляем вопрос всем игрокам
    send_question(head, q_index);
    
    time_t round_start = time(NULL);
    time_t current_time;
    int last_printed_sec = TIME_PER_QUESTION;
    int round_active = 1;
    char buffer[256];
    
    // Главный цикл раунда с таймером
    while (round_active == 1) {
        time_t now = time(NULL);
        int time_left = TIME_PER_QUESTION - (int)(now - round_start);

        // Печать отсчета, если осталось меньше 10 секунд
        if (time_left <= 10 && time_left != last_printed_sec) {
            snprintf(buffer, sizeof(buffer), "%d seconds left...\n", time_left);
            printf("%s", buffer);
            send_to_all_except(head, buffer, -1);
            last_printed_sec = time_left;
        }
    
        int active_players = 0;
        Player *cur = head;
        while (cur) { if (!cur->answered) active_players++; cur = cur->next; }

        if (active_players == 0) {
            round_active = 0;
            break;
        }

        // Формируем массив pollfd
        struct pollfd fds[active_players];
        Player *players_list[active_players]; // чтобы потом сопоставлять с игроками
        int idx = 0;
        cur = head;
        while (cur) {
            if (!cur->answered) {
                fds[idx].fd = cur->sock;
                fds[idx].events = POLLIN;
                players_list[idx] = cur;
                idx++;
            }
            cur = cur->next;
        }

        int timeout_ms = 100; // проверка каждые 100 мс
        int ready = poll(fds, active_players, timeout_ms);


        if (ready > 0) {
            for (int i = 0; i < active_players; i++) {
                cur = players_list[i];
                if ((fds[i].revents & POLLIN) && !cur->answered) {
                    char buf[10];
                    int n = recv(cur->sock, buf, sizeof(buf)-1, 0);

                    if (n > 0) {
                        buf[n] = '\0';
                        clean_string(buf);

                        if (strcmp(buf, "0") == 0) {
                            printf("🎮 %s не ответил вовремя (таймаут)\n", cur->name);
                            cur->answered = 1;
                            cur->answer = 0;
                            cur->answer_time = TIME_PER_QUESTION;
                        } else {
                            int answer = atoi(buf);
                            if (answer >= 1 && answer <= 4) {
                                int time_spent = (int)(now - round_start);
                                if (time_spent < 0) time_spent = 0;
                                if (time_spent > TIME_PER_QUESTION) time_spent = TIME_PER_QUESTION;

                                cur->answered = 1;
                                cur->answer = answer;
                                cur->answer_time = time_spent;

                                int is_correct = (answer == questions[q_index].correct_option);
                                int points = calculate_score(is_correct, time_spent);

                                cur->score += points;

                                char result_msg[256];
                                if (is_correct)
                                    snprintf(result_msg, sizeof(result_msg), "\n✅ Правильно! +%d очков\n", points);
                                else
                                    snprintf(result_msg, sizeof(result_msg),
                                             "\n❌ Неправильно. Правильный ответ: %d) %s\n",
                                             questions[q_index].correct_option,
                                             questions[q_index].options[questions[q_index].correct_option - 1]);
                                send(cur->sock, result_msg, strlen(result_msg), 0);

                                printf("🎮 %s ответил за %d сек (%s, +%d очков)\n",
                                       cur->name, time_spent,
                                       is_correct ? "правильно" : "неправильно",
                                       points);
                            }
                        }
                    } else if (n == 0) {
                        printf("❌ %s отключился\n", cur->name);
                        head = remove_player(head, cur->sock);
                    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("recv");
                    }
                }
            }
        }

        // Проверка таймера раунда
        if ((int)(now - round_start) >= TIME_PER_QUESTION) {
            printf("⏰ Время вышло!\n");
            round_active = 0;
            break;
        }

        // Проверка, все ли ответили
        cur = head;
        int all_answered = 1;
        while (cur) { if (!cur->answered) { all_answered = 0; break; } cur = cur->next; }
        if (all_answered) {
            printf("✅ Все игроки ответили за %ld секунд\n", now - round_start);
            round_active = 0;
            break;
        }
    }
    // Отправляем сообщение об окончании времени тем, кто не ответил
    Player *cur = head;
    while (cur) {
        if (!cur->answered) {
            char timeout_msg[512];
            snprintf(timeout_msg, sizeof(timeout_msg),
                    "\n⏰ Время вышло! Вы не успели ответить.\n"
                    "Правильный ответ: %d) %s\n\n"
                    "Переходим к следующему вопросу...\n",
                    questions[q_index].correct_option,
                    questions[q_index].options[questions[q_index].correct_option - 1]);

            send(cur->sock, timeout_msg, strlen(timeout_msg), 0);

        }
        cur = cur->next;
    }
    
    // Небольшая пауза, чтобы игроки успели прочитать сообщение
    usleep(1000000); // 1 секунда
}

Player* sort_players_by_score(Player *head, int *out_count) {
    // Считаем количество игроков
    int count = 0;
    Player *cur = head;
    while (cur) {
        count++;
        cur = cur->next;
    }
    *out_count = count;

    if (count == 0) return NULL;

    // Копируем игроков в массив
    Player *arr = malloc(count * sizeof(Player));
    if (!arr) return NULL;

    cur = head;
    for (int i = 0; i < count; i++) {
        arr[i] = *cur;
        cur = cur->next;
    }

    // Сортировка пузырьком по score (убывание)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (arr[j].score > arr[i].score) {
                Player temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
    }

    return arr;
}

// Отправка результатов игрокам (без времени)
void send_results(Player *head, int q_index) {
    char buffer[2048];

    int count = 0;
    Player *sorted_players = sort_players_by_score(head, &count);
    if (!sorted_players) return;

    // Формируем таблицу результатов
    snprintf(buffer, sizeof(buffer),
             "\n════════════════════════════════════════\n"
             "📊 РЕЗУЛЬТАТЫ ПОСЛЕ ВОПРОСА %d/%d\n"
             "════════════════════════════════════════\n"
             "┌──────────────────┬────────────┐\n"
             "│ Игрок            │ Очки       │\n"
             "├──────────────────┼────────────┤\n",
             q_index + 1, question_count);

    for (int i = 0; i < count; i++) {
        char line[100];
        snprintf(line, sizeof(line), "│ %-16s │ %-10d │\n",
                 sorted_players[i].name,
                 sorted_players[i].score);
        strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
    }

    strcat(buffer, "└──────────────────┴────────────┘\n\n");

    // Отправляем всем игрокам
    Player *cur = head;
    while (cur) {
        send(cur->sock, buffer, strlen(buffer), 0);
        cur = cur->next;
    }

    free(sorted_players);

    // Небольшая пауза перед следующим вопросом
    sleep(3);
}

void send_final_results(Player *head) {
    if (!head) return;

    // Сортируем игроков по очкам
    int count = 0;
    Player *sorted_players = sort_players_by_score(head, &count);
    if (!sorted_players || count == 0) return;

    // Определяем максимальный счет и количество победителей
    int max_score = sorted_players[0].score;
    int winner_count = 0;
    for (int i = 0; i < count; i++) {
        if (sorted_players[i].score == max_score) {
            winner_count++;
        }
    }

    // Формируем заголовок с поздравлением
    char buffer[8192];
    snprintf(buffer, sizeof(buffer),
             "\n══════════════════════════════════════════════════════════\n"
             "                      🎉 ИГРА ОКОНЧЕНА! 🎉\n"
             "══════════════════════════════════════════════════════════\n\n");

    // Добавляем поздравление победителю/победителям
    if (winner_count == 1) {
        char congrats[256];
        snprintf(congrats, sizeof(congrats),
                 "              🏆 ПОБЕДИТЕЛЬ: %-16s 🏆\n"
                 "              🏆 %d очков\n\n",
                 sorted_players[0].name, max_score);
        strcat(buffer, congrats);
    } else if (winner_count > 1) {
        char congrats[512];
        snprintf(congrats, sizeof(congrats),
                 "              🏆 ПОБЕДИТЕЛИ: \n");
        strcat(buffer, congrats);

        for (int i = 0; i < winner_count; i++) {
            char line[128];
            snprintf(line, sizeof(line), "              🏆 %-16s 🏆\n", sorted_players[i].name);
            strcat(buffer, line);
        }
        char score_line[128];
        snprintf(score_line, sizeof(score_line), "              %d очков\n\n", max_score);
        strcat(buffer, score_line);
    }

    // Добавляем итоговую таблицу
    strcat(buffer, "📈 ИТОГОВАЯ ТАБЛИЦА РЕЗУЛЬТАТОВ:\n"
                   "┌───────┬──────────────────┬────────────┬──────────────┐\n"
                   "│ Место │ Игрок            │ Очки       │ Рейтинг      │\n"
                   "├───────┼──────────────────┼────────────┼──────────────┤\n");

    for (int i = 0; i < count; i++) {
        char rating[20];
        if (i == 0 && sorted_players[i].score > 0) strcpy(rating, "⭐⭐⭐⭐⭐");
        else if (i == 1 && sorted_players[i].score > 0) strcpy(rating, "⭐⭐⭐⭐");
        else if (i == 2 && sorted_players[i].score > 0) strcpy(rating, "⭐⭐⭐");
        else if (sorted_players[i].score > 0) strcpy(rating, "⭐⭐");
        else strcpy(rating, "⭐");

        char line[128];
        snprintf(line, sizeof(line), "│ %-5d │ %-16s │ %-10d │ %-8s │\n",
                 i + 1, sorted_players[i].name, sorted_players[i].score, rating);
        strcat(buffer, line);
    }

    strcat(buffer, "└───────┴──────────────────┴────────────┴──────────────┘\n\n");

    // Статистика
    char stats[256];
    snprintf(stats, sizeof(stats),
             "📊 СТАТИСТИКА ИГРЫ:\n"
             "   Всего вопросов: %d\n"
             "   Всего игроков: %d\n"
             "   Максимальный счет: %d очков\n\n",
             question_count, count, max_score);
    strcat(buffer, stats);

    strcat(buffer,
           "══════════════════════════════════════════════════════════\n"
           "  Спасибо за участие в QuizRush! Ждем вас снова! 🎮\n"
           "══════════════════════════════════════════════════════════\n");

    // Отправляем всем игрокам
    send_to_all_except(head, buffer, -1);

    free(sorted_players);
    sleep(3);
}





int main() {
    if (!load_questions(QUESTIONS_FILE)) {
        printf("Используем вопросы по умолчанию...\n");
    }

    struct sockaddr_in server_addr;
    

    // Создаем серверный сокет
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    signal(SIGINT, handle_sigint);


    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("listen"); exit(1);
    }

    printf("Сервер запущен на порту %d\n", PORT);
    print_local_ip();
    printf("Сервер ждет игроков %d секунд...\n", CONNECT_TIMEOUT);

    int next_id = 1;

    struct pollfd fds[MAX_PLAYERS + 1];
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    time_t start_time = time(NULL);
    int player_count = 0;
    int last_second_printed = -1;

    char msg[256];

    PendingPlayer pending[MAX_PLAYERS];
    int pending_count = 0;

    while ((time(NULL) - start_time) < CONNECT_TIMEOUT && (player_count + pending_count) < MAX_PLAYERS) {
        int time_left = CONNECT_TIMEOUT - (int)(time(NULL) - start_time);

        if (time_left != last_second_printed && time_left <= 15) {
            printf("⏳ Осталось %d секунд для подключения игроков...\n", time_left);
            snprintf(msg, sizeof(msg), "⏳ Осталось %d секунд для подключения игроков...\n", time_left);
            send_to_all_except(head, msg, -1);
            send_to_pending(pending, pending_count, msg);
            last_second_printed = time_left;
        }

        int nfds = 1 + pending_count;
        for (int i = 0; i < pending_count; i++) {
            fds[i+1].fd = pending[i].sock;
            fds[i+1].events = POLLIN;
        }

        int ready = poll(fds, nfds, 100); // проверяем каждые 100 мс
        if (ready > 0) {
            // Новый клиент
            if (fds[0].revents & POLLIN && (player_count + pending_count) < MAX_PLAYERS) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_sock >= 0) {
                    fcntl(client_sock, F_SETFL, O_NONBLOCK); // неблокирующий режим
                    pending[pending_count].sock = client_sock;
                    pending[pending_count].bytes_received = 0;
                    pending_count++;
                }
            }

            // Чтение имён у ожидающих игроков
            for (int i = 0; i < pending_count; i++) {
                char buf[MAX_NAME_LEN];
                int n = recv(pending[i].sock, buf, sizeof(buf)-1, 0);
                if (n > 0) {
                    buf[n] = '\0';
                    clean_string(buf);
                    strncpy(pending[i].name, buf, MAX_NAME_LEN-1);
                    pending[i].name[MAX_NAME_LEN-1] = '\0';

                    // Проверка дубликатов
                    int suffix = 1;
                    char original[MAX_NAME_LEN];
                    strncpy(original, pending[i].name, MAX_NAME_LEN-1);
                    original[MAX_NAME_LEN-1] = '\0';
                    while (name_exists(head, pending[i].name)) {
                        snprintf(pending[i].name, MAX_NAME_LEN, "%s_%d", original, suffix++);
                    }

                    // Добавляем игрока в список
                    head = add_player(head, pending[i].sock, pending[i].name, player_count + 1);
                    player_count++;

                    snprintf(msg, sizeof(msg), "Привет, %s! Вы присоединились к игре.\n", pending[i].name);
                    send(pending[i].sock, msg, strlen(msg), 0);
                    snprintf(msg, sizeof(msg), "%s подключился (%d/%d)\n", pending[i].name, player_count, MAX_PLAYERS);
                    printf("%s подключился (%d/%d)\n", pending[i].name, player_count, MAX_PLAYERS);
                    send_to_all_except(head, msg, next_id-1);

                    // Убираем из массива pending
                    for (int j = i; j < pending_count - 1; j++) pending[j] = pending[j+1];
                    pending_count--;
                    i--; // смещаем индекс
                }
            }
        }

        // Проверка таймера подключения для игроков без имени
        for (int i = 0; i < pending_count; i++) {
            if ((time(NULL) - start_time) >= CONNECT_TIMEOUT) {
                printf("❌ Игрок на сокете %d не успел ввести имя, соединение закрыто\n", pending[i].sock);
                close(pending[i].sock);

                // Убираем из массива
                for (int j = i; j < pending_count - 1; j++) pending[j] = pending[j+1];
                pending_count--;
                i--;
            }
        }
    }

    printf("Время ожидания истекло. Игроков подключено: %d\n", player_count);
    send_to_all_except(head, "Все игроки подключены! Игра начинается!\n", -1);

    // Викторина
    for (int q = 0; q < question_count; q++) {
        process_round(head, q);
        send_results(head, q);
    }

    // Финальные результаты
    send_final_results(head);

    // Закрываем соединения
    free_players(head);
    close(server_fd);

    printf("Игра окончена!\n");
    return 0;
}


#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

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

typedef struct Player {
  int id;
  int sock;
  char name[MAX_NAME_LEN];
  int score;
  int answered;
  int answer;
  int answer_time;
  int ready;
  int connected;
  struct Player *next;
} Player;

typedef struct {
  int sock;
  char name[MAX_NAME_LEN];
  int bytes_received;
} PendingPlayer;

Player *head = NULL;
int server_fd = -1;
Question *questions = NULL;
int question_count = 0;

void send_to_all_except(Player *head, const char *msg, int exclude_id);
void free_players(Player *head);

Player *add_player(Player *head, int sock, const char *name, int id) {
  Player *p = malloc(sizeof(Player));
  if (!p) {
    perror("malloc");
    exit(1);
  }
  p->id = id;
  p->sock = sock;
  strncpy(p->name, name, sizeof(p->name) - 1);
  p->name[sizeof(p->name) - 1] = '\0';
  p->score = 0;
  p->answered = 0;
  p->ready = 0;
  p->connected = 1;
  p->next = NULL;

  if (!head)
    return p;

  Player *cur = head;
  while (cur->next)
    cur = cur->next;
  cur->next = p;
  return head;
}

Player *remove_player(Player *head, int sock) {
  Player *cur = head;
  Player *prev = NULL;

  while (cur) {
    if (cur->sock == sock) {
      if (prev)
        prev->next = cur->next;
      else
        head = cur->next;

      close(cur->sock);
      free(cur);
      return head;
    }
    prev = cur;
    cur = cur->next;
  }
  return head;
}

void handle_sigint() {
  printf("\nСервером получен сигнал для завершения, закрываем соединения...\n");

  if (head) {
    send_to_all_except(head, "\nСервер завершает работу. Игра остановлена.\n",
                       -1);
  }

  if (head)
    free_players(head);

  if (server_fd >= 0)
    close(server_fd);

  free(questions);
  exit(0);
}

Player *cleanup_disconnected(Player *head) {
  Player *cur = head;
  Player *prev = NULL;

  while (cur) {
    if (!cur->connected) {
      Player *dead = cur;
      if (prev)
        prev->next = cur->next;
      else
        head = cur->next;

      cur = cur->next;
      close(dead->sock);
      free(dead);
    } else {
      prev = cur;
      cur = cur->next;
    }
  }
  return head;
}

void send_to_all_except(Player *head, const char *msg, int exclude_id) {
  Player *cur = head;
  while (cur) {
    if (cur->connected && cur->id != exclude_id) {
      ssize_t s = send(cur->sock, msg, strlen(msg), 0);
      if (s <= 0) {
        cur->connected = 0;
      }
    }
    cur = cur->next;
  }
}

void notify_about_disconnected(Player *head) {
  Player *cur = head;
  char msg[256];

  while (cur) {
    if (!cur->connected) {
      snprintf(msg, sizeof(msg), "[%s] покинул игру\n", cur->name);
      send_to_all_except(head, msg, cur->id);
    }
    cur = cur->next;
  }
}

void send_to_pending(PendingPlayer *pending, int count, const char *msg) {
  for (int i = 0; i < count; i++) {
    send(pending[i].sock, msg, strlen(msg), 0);
  }
}

void free_players(Player *head) {
  Player *cur = head;
  while (cur) {
    Player *tmp = cur;
    cur = cur->next;
    close(tmp->sock);
    free(tmp);
  }
}

/* i dislike this func tbh...
 * if we call this twice or more, we will generate a memory leak
 * and lose all our data
 * and if we dont call func twice or more, why we need this func???
 */
int load_questions(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    printf("Ошибка: не удалось открыть файл %s\n", filename);
    return 0;
  }

  char line[MAX_QUESTION_LEN * 2];
  int count = 0;

  while (fgets(line, sizeof(line), file)) {
    if (strlen(line) > 1)
      count++;
  }
  question_count = count / 6;

  questions = malloc(question_count * sizeof(Question));
  if (!questions) {
    fclose(file);
    return 0;
  }

  rewind(file);
  for (int i = 0; i < question_count; i++) {
    fgets(questions[i].question, MAX_QUESTION_LEN, file);
    questions[i].question[strcspn(questions[i].question, "\n")] = 0;

    for (int j = 0; j < OPTIONS_COUNT; j++) {
      fgets(questions[i].options[j], MAX_ANSWER_LEN, file);
      questions[i].options[j][strcspn(questions[i].options[j], "\n")] = 0;
    }

    fgets(line, sizeof(line), file);
    sscanf(line, "%d", &questions[i].correct_option);
  }

  fclose(file);
  printf("Загружено вопросов: %d\n", question_count);
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

int name_exists(Player *head, const char *name) {
  Player *cur = head;
  while (cur) {
    if (strcmp(cur->name, name) == 0) {
      return 1;
    }
    cur = cur->next;
  }
  return 0;
}

int calculate_score(int is_correct, int time_spent) {
  if (!is_correct)
    return 0;

  int time_bonus = TIME_PER_QUESTION - time_spent;
  if (time_bonus < 0)
    time_bonus = 0;

  return BASE_POINTS + time_bonus;
}

void send_question(Player *head, int q_index) {
  char buffer[1024];
  Question q = questions[q_index];

  snprintf(buffer, sizeof(buffer),
           "\n=================================================\n"
           "Вопрос %d/%d:\n"
           "%s\n\n"
           "Варианты ответов:\n"
           "1) %s\n"
           "2) %s\n"
           "3) %s\n"
           "4) %s\n\n"
           "У вас есть %d секунд! Введите номер ответа (1-4): \n",
           q_index + 1, question_count, q.question, q.options[0], q.options[1],
           q.options[2], q.options[3], TIME_PER_QUESTION);

  send_to_all_except(head, buffer, -1);
}

int all_players_answered(Player *head) {
  Player *cur = head;
  while (cur) {
    if (cur->connected && !cur->answered) {
      return 0;
    }
    cur = cur->next;
  }
  return 1;
}

void reset_round_flags(Player *head) {
  Player *cur = head;
  while (cur) {
    cur->answered = 0;
    cur->answer = 0;
    cur->answer_time = 0;
    cur = cur->next;
  }
}

void process_round(Player *head, int q_index) {
  printf("\nВопрос %d/%d: %s\n", q_index + 1, question_count,
         questions[q_index].question);

  reset_round_flags(head);
  send_question(head, q_index);

  time_t round_start = time(NULL);
  int round_active = 1;
  int last_printed_sec = TIME_PER_QUESTION;
  char buffer[256];

  while (round_active) {
    time_t now = time(NULL);
    int time_left = TIME_PER_QUESTION - (int)(now - round_start);

    if (time_left <= 10 && time_left != last_printed_sec) {
      snprintf(buffer, sizeof(buffer), "До окончания раунда: %d...\n",
               time_left);
      printf("%s", buffer);
      send_to_all_except(head, buffer, -1);
      last_printed_sec = time_left;
    }

    int active_players = 0;
    Player *cur = head;
    while (cur) {
      if (cur->connected && !cur->answered)
        active_players++;
      cur = cur->next;
    }

    if (active_players == 0 || (int)(now - round_start) >= TIME_PER_QUESTION) {
      round_active = 0;
      break;
    }

    struct pollfd fds[active_players + 1];
    Player *players_list[active_players];
    int idx = 0;

    fds[idx].fd = server_fd;
    fds[idx].events = POLLIN;
    idx++;

    cur = head;
    int pidx = 0;
    while (cur) {
      if (cur->connected && !cur->answered) {
        fds[idx].fd = cur->sock;
        fds[idx].events = POLLIN;
        players_list[pidx++] = cur;
        idx++;
      }
      cur = cur->next;
    }

    int ready = poll(fds, idx, 100);
    if (ready < 0 && errno != EINTR) {
      perror("poll");
      continue;
    }

    if (fds[0].revents & POLLIN) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int new_sock =
          accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (new_sock >= 0) {
        fcntl(new_sock, F_SETFL, O_NONBLOCK);
        char *msg = "Игра уже идет! Попробуйте позже.\n";
        send(new_sock, msg, strlen(msg), 0);
        close(new_sock);
        printf("Игрок попытался подключиться во время игры, соединение "
               "закрыто.\n");
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("accept");
      }
    }

    for (int i = 0; i < active_players; i++) {
      cur = players_list[i];
      if ((fds[i + 1].revents & POLLIN) && !cur->answered) {
        char buf[10];
        int n = recv(cur->sock, buf, sizeof(buf) - 1, 0);

        if (n > 0) {
          buf[n] = '\0';
          clean_string(buf);

          if (strcmp(buf, "0") == 0) {
            printf("[%s] не ответил вовремя\n", cur->name);
            cur->answered = 1;
            cur->answer = 0;
            cur->answer_time = TIME_PER_QUESTION;
          } else {
            int answer = atoi(buf);
            if (answer >= 1 && answer <= 4) {
              int time_spent = (int)(now - round_start);
              if (time_spent < 0)
                time_spent = 0;
              if (time_spent > TIME_PER_QUESTION)
                time_spent = TIME_PER_QUESTION;

              cur->answered = 1;
              cur->answer = answer;
              cur->answer_time = time_spent;

              int is_correct = (answer == questions[q_index].correct_option);
              int points = calculate_score(is_correct, time_spent);

              cur->score += points;

              char result_msg[256];
              if (is_correct)
                snprintf(result_msg, sizeof(result_msg), "\nПравильно! +%d\n",
                         points);
              else
                snprintf(result_msg, sizeof(result_msg),
                         "\nНеправильно. Правильный ответ: %d) %s\n",
                         questions[q_index].correct_option,
                         questions[q_index]
                             .options[questions[q_index].correct_option - 1]);
              ssize_t s = send(cur->sock, result_msg, strlen(result_msg), 0);
              if (s <= 0) {
                printf("[%s] отключился между раундами\n", cur->name);
                cur->connected = 0;
              }

              printf("[%s] ответил за %d сек (%s, +%d)\n", cur->name,
                     time_spent, is_correct ? "правильно" : "неправильно",
                     points);
            }
          }
        } else if (n == 0) {
          printf("[%s] отключился\n", cur->name);
          cur->connected = 0;
          cur->answered = 1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("recv");
        }
      }
    }
  }

  Player *cur = head;
  while (cur) {
    if (!cur->answered) {
      char timeout_msg[512];
      snprintf(
          timeout_msg, sizeof(timeout_msg),
          "\nВремя вышло! Вы не успели ответить.\n"
          "Правильный ответ: %d) %s\n\n",
          questions[q_index].correct_option,
          questions[q_index].options[questions[q_index].correct_option - 1]);
      ssize_t s = send(cur->sock, timeout_msg, strlen(timeout_msg), 0);
      if (s <= 0) {
        printf("[%s] отключился\n", cur->name);
        cur->connected = 0;
      }
    }
    cur = cur->next;
  }
  char msg[256];
  snprintf(msg, sizeof(msg),
           "Все игроки ответили. Переходим к следующему вопросу...\n");
  send_to_all_except(head, msg, -1);
}

Player *sort_players_by_score(Player *head, int *out_count) {
  int count = 0;
  Player *cur = head;
  while (cur) {
    count++;
    cur = cur->next;
  }
  *out_count = count;

  if (count == 0)
    return NULL;

  Player *arr = malloc(count * sizeof(Player));
  if (!arr)
    return NULL;

  cur = head;
  for (int i = 0; i < count; i++) {
    arr[i] = *cur;
    cur = cur->next;
  }

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

void send_results(Player *head, int q_index) {
  char buffer[2048];

  int count = 0;
  Player *sorted_players = sort_players_by_score(head, &count);
  if (!sorted_players)
    return;

  snprintf(buffer, sizeof(buffer),
           "\n════════════════════════════════════════\n"
           " РЕЗУЛЬТАТЫ ПОСЛЕ ВОПРОСА %d/%d\n"
           "════════════════════════════════════════\n"
           "┌──────────────────┬────────────┐\n"
           "│ Игрок            │ Очки       │\n"
           "├──────────────────┼────────────┤\n",
           q_index + 1, question_count);

  for (int i = 0; i < count; i++) {
    char line[100];
    snprintf(line, sizeof(line), "│ %-16s │ %-10d │\n", sorted_players[i].name,
             sorted_players[i].score);
    strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
  }

  strcat(buffer, "└──────────────────┴────────────┘\n\n");

  Player *cur = head;
  while (cur) {
    ssize_t s = send(cur->sock, buffer, strlen(buffer), 0);
    if (s <= 0) {
      printf("[%s] отключился\n", cur->name);
      cur->connected = 0;
    }
    cur = cur->next;
  }

  free(sorted_players);

  sleep(3);
}

void send_final_results(Player *head) {
  if (!head)
    return;

  int count = 0;
  Player *sorted_players = sort_players_by_score(head, &count);
  if (!sorted_players || count == 0)
    return;

  int max_score = sorted_players[0].score;
  int winner_count = 0;
  for (int i = 0; i < count; i++) {
    if (sorted_players[i].score == max_score) {
      winner_count++;
    }
  }

  char buffer[8192];
  snprintf(buffer, sizeof(buffer),
           "\n══════════════════════════════════════════════════════════\n"
           "                        ИГРА ОКОНЧЕНА! \n"
           "══════════════════════════════════════════════════════════\n\n");

  if (winner_count == 1) {
    char congrats[256];
    snprintf(congrats, sizeof(congrats),
             "                ПОБЕДИТЕЛЬ: %-16s \n"
             "                   Счёт:%d \n\n",
             sorted_players[0].name, max_score);
    strcat(buffer, congrats);
  } else if (winner_count > 1) {
    char congrats[512];
    snprintf(congrats, sizeof(congrats), "                ПОБЕДИТЕЛИ: \n");
    strcat(buffer, congrats);

    for (int i = 0; i < winner_count; i++) {
      char line[128];
      snprintf(line, sizeof(line), "                %-16s  \n",
               sorted_players[i].name);
      strcat(buffer, line);
    }
    char score_line[128];
    snprintf(score_line, sizeof(score_line), "              %d \n\n",
             max_score);
    strcat(buffer, score_line);
  }

  strcat(buffer, "📈 ИТОГОВАЯ ТАБЛИЦА РЕЗУЛЬТАТОВ:\n"
                 "┌───────┬──────────────────┬────────────┐\n"
                 "│ Место │ Игрок            │ Очки       │\n"
                 "├───────┼──────────────────┼────────────│\n");

  for (int i = 0; i < count; i++) {
    char line[128];
    snprintf(line, sizeof(line), "│ %-5d │ %-16s │ %-10d │\n", i + 1,
             sorted_players[i].name, sorted_players[i].score);
    strcat(buffer, line);
  }

  strcat(buffer, "└───────┴──────────────────┴────────────┘\n\n");

  char stats[256];
  snprintf(stats, sizeof(stats),
           "  СТАТИСТИКА ИГРЫ:\n"
           "   Всего вопросов: %d\n"
           "   Всего игроков: %d\n"
           "   Максимальный счет: %d \n\n",
           question_count, count, max_score);
  strcat(buffer, stats);

  strcat(buffer,
         "══════════════════════════════════════════════════════════\n"
         "  Спасибо за участие в QuizRush! Ждем вас снова! \n"
         "══════════════════════════════════════════════════════════\n");

  send_to_all_except(head, buffer, -1);

  free(sorted_players);
  sleep(3);
}

int main() {
  if (!load_questions(QUESTIONS_FILE)) {
    handle_sigint();
  }

  struct sockaddr_in server_addr;

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(1);
  }

  signal(SIGINT, handle_sigint);
  signal(SIGPIPE, SIG_IGN);

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  fcntl(server_fd, F_SETFL, O_NONBLOCK);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind");
    exit(1);
  }

  if (listen(server_fd, MAX_PLAYERS) < 0) {
    perror("listen");
    exit(1);
  }

  printf("Сервер запущен на порту %d\n", PORT);

  PendingPlayer pending[MAX_PLAYERS];
  int pending_count = 0;

  // we cant reinitialize global variable in `main`!
  int next_id = 1;

  printf("Ожидаем игроков в лобби...\n");

  while (1) {
    int count = 0;
    Player *cur = head;
    while (cur) {
      count++;
      cur = cur->next;
    }

    int nfds = 1 + pending_count + count;
    struct pollfd fds[nfds];
    int idx = 0;

    fds[idx].fd = server_fd;
    fds[idx].events = POLLIN;
    idx++;

    for (int i = 0; i < pending_count; i++, idx++) {
      fds[idx].fd = pending[i].sock;
      fds[idx].events = POLLIN;
    }

    cur = head;
    Player *players[count];
    int pidx = 0;
    while (cur) {
      fds[idx].fd = cur->sock;
      fds[idx].events = POLLIN;
      players[pidx++] = cur;
      cur = cur->next;
      idx++;
    }

    int ready_count = poll(fds, nfds, 100);
    if (ready_count < 0 && errno != EINTR) {
      perror("poll");
      continue;
    }

    if (fds[0].revents & POLLIN && (count + pending_count) < MAX_PLAYERS) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_sock =
          accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
      if (client_sock >= 0) {
        fcntl(client_sock, F_SETFL, O_NONBLOCK);
        pending[pending_count].sock = client_sock;
        pending[pending_count].bytes_received = 0;
        pending[pending_count].name[0] = '\0';
        pending_count++;
      }
    }

    for (int i = 0; i < pending_count; i++) {
      char buf[MAX_NAME_LEN];
      int n = recv(pending[i].sock, buf, sizeof(buf) - 1, 0);
      if (n > 0) {
        buf[n] = '\0';
        clean_string(buf);
        strncpy(pending[i].name, buf, MAX_NAME_LEN - 1);
        pending[i].name[MAX_NAME_LEN - 1] = '\0';

        int suffix = 1;
        char original[MAX_NAME_LEN];
        strncpy(original, pending[i].name, MAX_NAME_LEN);
        while (name_exists(head, pending[i].name)) {
          snprintf(pending[i].name, MAX_NAME_LEN, "%s_%d", original, suffix++);
        }

        head = add_player(head, pending[i].sock, pending[i].name, next_id++);
        printf("Игрок [%s] добавлен в игру!\n", pending[i].name);
        int ready = 0;
        int total_players = 0;
        cur = head;
        while (cur) {
          total_players++;
          if (cur->ready)
            ready++;
          cur = cur->next;
        }
        char msg[256];
        snprintf(
            msg, sizeof(msg),
            "[%s] присоединился! Готовых игроков на данный момент: (%d/%d)\n",
            pending[i].name, ready, total_players);
        send_to_all_except(head, msg, -1);
        snprintf(msg, sizeof(msg),
                 "Для подтверждения готовности введите комманду '/ready'\n");
        send(pending[i].sock, msg, strlen(msg), 0);
        for (int j = i; j < pending_count - 1; j++)
          pending[j] = pending[j + 1];
        pending_count--;
        i--;
      } else if (n == 0) {
        close(pending[i].sock);
        for (int j = i; j < pending_count - 1; j++)
          pending[j] = pending[j + 1];
        pending_count--;
        i--;
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv");
      }
    }

    for (int i = 0; i < count; i++) {
      if (fds[1 + pending_count + i].revents & POLLIN) {
        char msg[64];
        int n = recv(fds[1 + pending_count + i].fd, msg, sizeof(msg) - 1, 0);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("recv");
          close(players[i]->sock);
          players[i]->connected = 0;
        } else if (n == 0) {
          printf("Игрок [%s] отключился\n", players[i]->name);
          char s_name[MAX_NAME_LEN];
          snprintf(s_name, sizeof(s_name), "%s", players[i]->name);
          head = remove_player(head, players[i]->sock);
          if (!head) {
            handle_sigint();
          }
          char buffer[256];
          int ready = 0;
          int total_players = 0;
          cur = head;
          while (cur) {
            total_players++;
            if (cur->ready)
              ready++;
            cur = cur->next;
          }
          snprintf(buffer, sizeof(buffer),
                   "Игрок [%s] вышел из лобби. Готовые игроки: (%d/%d)\n",
                   s_name, ready, total_players);
          send_to_all_except(head, buffer, -1);
        } else if (n > 0) {
          msg[n] = '\0';
          clean_string(msg);
          if (strcmp(msg, "/ready") == 0 && !players[i]->ready) {
            players[i]->ready = 1;
            char buffer[256];
            int ready = 0;
            int total_players = 0;
            cur = head;
            while (cur) {
              total_players++;
              if (cur->ready)
                ready++;
              cur = cur->next;
            }
            snprintf(buffer, sizeof(buffer),
                     "[%s] готов. Готовые игроки: (%d/%d)\n", players[i]->name,
                     ready, total_players);
            send_to_all_except(head, buffer, -1);
          }
        }
      }
    }

    int all_ready = 1;
    int total_players = 0;
    cur = head;
    while (cur) {
      total_players++;
      if (!cur->ready)
        all_ready = 0;
      cur = cur->next;
    }

    if (total_players > 0 && all_ready) {
      send_to_all_except(head, "\nВсе игроки готовы! Игра начинается...\n", -1);
      sleep(3);
      break;
    }
  }
  for (int i = 0; i < pending_count; i++) {
    close(pending[i].sock);
  }
  printf("Старт игры!\n");

  for (int q = 0; q < question_count; q++) {

    process_round(head, q);
    notify_about_disconnected(head);
    head = cleanup_disconnected(head);
    if (!head)
      handle_sigint();

    send_results(head, q);
    notify_about_disconnected(head);
    head = cleanup_disconnected(head);
    if (!head)
      handle_sigint();
    sleep(2);
  }

  send_final_results(head);

  free_players(head);
  free(questions);
  close(server_fd);

  printf("Игра окончена!\n");
  return 0;
}

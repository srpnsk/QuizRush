# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -g

# Названия исполняемых файлов
SERVER = server
CLIENT = client

# Исходники
SRCS_SERVER = server.c
SRCS_CLIENT = client.c

# Правило по умолчанию
all: $(SERVER) $(CLIENT)

# Сборка сервера
$(SERVER): $(SRCS_SERVER)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRCS_SERVER)

# Сборка клиента
$(CLIENT): $(SRCS_CLIENT)
	$(CC) $(CFLAGS) -o $(CLIENT) $(SRCS_CLIENT)

# Очистка временных файлов
clean:
	rm -f $(SERVER) $(CLIENT)

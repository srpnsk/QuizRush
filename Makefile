CC = gcc
CFLAGS = -Wall -Wextra -g

SERVER = server.out
CLIENT = client.out

SRCS_SERVER = server.c
SRCS_CLIENT = client.c

all: $(SERVER) $(CLIENT)

$(SERVER): $(SRCS_SERVER)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRCS_SERVER)

$(CLIENT): $(SRCS_CLIENT)
	$(CC) $(CFLAGS) -o $(CLIENT) $(SRCS_CLIENT)

clean:
	rm -f $(SERVER) $(CLIENT)

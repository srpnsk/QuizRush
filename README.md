# QuizRush
**QuizRush** is a console-based multiplayer quiz game for a local network, written in C using TCP sockets and poll().
Players connect to a server via IP address and answer questions in real time.

## 📥 Download
Clone the repository or download the source files:
```sh
git clone https://github.com/KkonepHuk/QuizRush
cd QuizRush
```

## 🛠 Compilation
Compile the server and client programs:
```sh
gcc server.c -o s; gcc client.c -o c;
```
>⚠️ Requires Linux or macOS (POSIX environment).
>Uses BSD sockets and poll().

## ▶️ Running the Game
### Server
Run the server on one computer in the local network:
```sh
./s
```

The server will display:
- Hostname of the machine
- Local IP addresses for player connections
- A countdown timer while waiting for players

Example output:
```
Hostname: localhost
IP:
  192.168.0.104
Waiting for players (30 seconds)...
```

### Client
On another device in the same network, run the client using the server’s IP:
```sh
./c 192.168.0.104
```

The client will ask for your name:
```
Enter your name: Nikita
```

After connecting, the player will receive a welcome message and can start answering quiz questions.

## 🎮 How to Play
1. The server waits for players for a limited time (CONNECT_TIMEOUT)
2. Players enter their names (If a player does not enter a name in time, the connection is closed)
3. Once all players are connected, the quiz starts
4. Each question is sent to all players
5. Players enter their answers in the terminal
6. After each round, the server sends updated scores
7. After the last question, final results are displayed

## 🚀 Planned Features
1. Player Accounts (Very unlikely)
    - Optional feature to have personal accounts with username, password, and statistics.
    - Track progress and performance across multiple games.
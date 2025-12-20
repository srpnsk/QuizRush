# QuizRush
**QuizRush** is a simple multiplayer quiz game for a local network. Players connect via the server’s IP and answer questions in real-time.

## Download
Clone the repository or download the source files:
```sh
git clone https://github.com/KkonepHuk/QuizRush
cd QuizRush
```

## Compilation
Compile the server and client programs:
```sh
gcc server.c -o s; gcc client.c -o c;
```

## Running the Game
### Server
Run the server on one computer in the local network:
```sh
./s
```

The server will display:
- Hostname of the machine
- Local IP addresses for player connections
- A message indicating that it is waiting for players

Example output:
```
Hostname: localhost
IP:
  192.168.0.104
0/2 connected...
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

## How to Play
1. The server waits for all players to connect (default 2, configurable).
2. Each question is sent to all players sequentially.
3. Players input their answers in the console.
4. After each question, the server sends updated scores to all players.
5. The game ends after all questions are answered.

## Planned Features

We plan to add the following improvements in future versions:
1. Dynamic Number of Players
    - Currently the number of players is fixed.
    - Future versions will allow any number of players to join the game.
2. Enhanced Scoreboard
    - Display a visually appealing results table.
    - Sort players by the number of points in descending order after each question.
3. Timed Responses
    - Players will have a limited time to answer each question.
    - Faster correct answers will earn more points.
4. Player Accounts (Very unlikely)
    - Optional feature to have personal accounts with username, password, and statistics.
    - Track progress and performance across multiple games.
# Sieciowa gra w warcaby

## Opis projektu
Projekt to prosta sieciowa, turowa gra w warcaby napisana w języku C, wykorzystująca gniazda TCP do komunikacji między klientami a serwerem. Serwer obsługuje wiele równoległych rozgrywek, waliduje ruchy oraz obsługuje rozłączenie graczy.

## Wymagania systemowe
- Kompilator GCC
- System Linux (można też uruchomić na Windowsie z MinGW lub WSL)
- Biblioteki `pthread` oraz `arpa/inet.h` (dla połączeń sieciowych)

## Kompilacja i uruchomienie

### 1. Skompiluj serwer i klienta:
```bash
gcc server.c -o server -pthread
gcc client.c -o client
```

### 2. Uruchom serwer:
```bash
./server 12345
```
Gdzie `12345` to port, na którym serwer będzie nasłuchiwał.

### 3. Uruchom klientów (dwóch graczy):
W dwóch osobnych terminalach uruchom:
```bash
./client 127.0.0.1 12345
```
Jeśli serwer działa na innym komputerze w sieci, podaj jego adres IP zamiast `127.0.0.1`.

## Zasady gry
- Gracze poruszają się naprzemiennie.
- Ruchy wysyłane są w formacie:
  ```
  MOVE from_x from_y to_x to_y
  ```
  np.:
  ```
  MOVE 2 1 3 2
  ```
- Gra kończy się, gdy jeden z graczy nie ma już dostępnych ruchów lub jego pionki zostały zbite.
- Aby się poddać, wpisz:
  ```
  QUIT
  ```

## Obsługa błędów
- Jeśli serwer zwróci `ERR`, oznacza to błędny ruch.
- `WAIT` oznacza, że należy poczekać na swoją turę.
- `WIN`, `LOSE`, `DRAW` to komunikaty kończące grę.

## Możliwe usprawnienia
- Dodanie interfejsu graficznego.
- Obsługa bicia wielokrotnego.
- Możliwość wznowienia przerwanej gry.

## Autor
Kacper Mokrzycki, 151893
Projekt stworzony jako zaliczenie przedmiotu „Sieci komputerowe 2”.

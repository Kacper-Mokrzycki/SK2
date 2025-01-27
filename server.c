#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#include "common.h"

// Struktura opisująca jedną grę
typedef struct {
    int player1_socket;
    int player2_socket;
    // Plansza 8x8, wypełniona odpowiednimi polami
    FieldType board[BOARD_SIZE][BOARD_SIZE];
    int current_player; // 1 lub 2
    pthread_mutex_t game_mutex;
    int is_active; // flaga czy gra nadal trwa
} Game;

// Prosta lista gier
#define MAX_GAMES 50
static Game games[MAX_GAMES];
static pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;

// Kolejka oczekujących graczy (socket)
static int waiting_socket = -1; 
static pthread_mutex_t waiting_mutex = PTHREAD_MUTEX_INITIALIZER;

// Funkcja inicjująca stan planszy warcabowej
void init_board(FieldType board[BOARD_SIZE][BOARD_SIZE]) {
    // Ustawiamy wszystkie pola na EMPTY
    for(int i = 0; i < BOARD_SIZE; i++) {
        for(int j = 0; j < BOARD_SIZE; j++) {
            board[i][j] = EMPTY;
        }
    }

    // Uproszczone ustawienie pionków:
    // Białe (PAWN_WHITE) na górze (rzędy 0..2)
    // Czarne (PAWN_BLACK) na dole (rzędy 5..7)
    // Klasyczny układ w warcabach:
    for(int i = 0; i < BOARD_SIZE; i++) {
        for(int j = 0; j < BOARD_SIZE; j++) {
            if((i + j) % 2 == 1) { // tylko na czarnych polach
                if(i < 3) {
                    board[i][j] = PAWN_WHITE;
                } else if(i > 4) {
                    board[i][j] = PAWN_BLACK;
                }
            }
        }
    }
}

// Funkcja sprawdzająca, czy ruch jest poprawny.
// Dla uproszczenia obsługujemy tylko ruch "o jedno pole" i bicie "o dwa pola",
// bez wielokrotnego bicia itd.
int validate_move(Game *game, int from_x, int from_y, int to_x, int to_y) {
    // Wyciągnięcie z planszy wartości
    FieldType piece = game->board[from_x][from_y];
    if(piece == EMPTY) {
        return 0; // brak pionka w tym miejscu
    }

    // Sprawdzamy, czy ruch należy do aktualnego gracza
    int is_white = (piece == PAWN_WHITE || piece == KING_WHITE);
    int current_is_white = (game->current_player == 1);

    if(is_white != current_is_white) {
        return 0; // próba ruchu pionkiem przeciwnika
    }

    // Sprawdzamy, czy pole docelowe jest puste
    if(game->board[to_x][to_y] != EMPTY) {
        return 0; // pole zajęte
    }

    // Prosty ruch pionkiem – w zależności od koloru
    int dir = is_white ? 1 : -1; // białe "w dół", czarne "w górę"
    
    // Ruch bez bicia – o 1 w kierunku pionka (dla zwykłych pionków)
    if((from_x + dir == to_x) && (abs(from_y - to_y) == 1)) {
        // OK
        return 1;
    }

    // Sprawdzenie bicia (przeskok o 2 pola)
    if((from_x + 2*dir == to_x) && (abs(from_y - to_y) == 2)) {
        // Sprawdzamy, co jest na polu pomiędzy
        int mid_x = from_x + dir;
        int mid_y = (from_y + to_y) / 2;
        FieldType mid_piece = game->board[mid_x][mid_y];
        // Musi być przeciwnika
        if(mid_piece == EMPTY) return 0;
        if(is_white && (mid_piece == PAWN_BLACK || mid_piece == KING_BLACK)) {
            return 1;
        }
        if(!is_white && (mid_piece == PAWN_WHITE || mid_piece == KING_WHITE)) {
            return 1;
        }
        return 0;
    }

    // Obsługa królowek (KING_WHITE/KING_BLACK) – przykładowo ruch w dowolnym kierunku
    if(piece == KING_WHITE || piece == KING_BLACK) {
        // Królówka może ruszać się w 4 kierunkach po przekątnej o 1
        if(abs(from_x - to_x) == 1 && abs(from_y - to_y) == 1) {
            return 1; 
        }
        // Bicie królowką – przeskok o 2
        if(abs(from_x - to_x) == 2 && abs(from_y - to_y) == 2) {
            int mid_x = (from_x + to_x)/2;
            int mid_y = (from_y + to_y)/2;
            FieldType mid_piece = game->board[mid_x][mid_y];
            if(piece == KING_WHITE && (mid_piece == PAWN_BLACK || mid_piece == KING_BLACK)) {
                return 1;
            }
            if(piece == KING_BLACK && (mid_piece == PAWN_WHITE || mid_piece == KING_WHITE)) {
                return 1;
            }
        }
    }

    return 0; 
}

// Funkcja wykonująca ruch (o ile validate_move powiedziało, że jest poprawny).
// Zwraca 1 w przypadku bicia, 0 w przypadku zwykłego ruchu.
int make_move(Game *game, int from_x, int from_y, int to_x, int to_y) {
    FieldType piece = game->board[from_x][from_y];
    game->board[from_x][from_y] = EMPTY;
    game->board[to_x][to_y] = piece;

    // Sprawdzamy, czy było to bicie (różnica w x to 2 lub -2)
    if(abs(from_x - to_x) == 2) {
        // usuwamy pionek przeciwnika
        int mid_x = (from_x + to_x)/2;
        int mid_y = (from_y + to_y)/2;
        game->board[mid_x][mid_y] = EMPTY;
        return 1;
    }
    return 0;
}

// Sprawdzamy, czy któryś gracz wygrał (np. brak pionków przeciwnika).

// - jeśli nie ma pionków czarnych -> wygrywają białe
// - jeśli nie ma pionków białych -> wygrywają czarne
GameStatus check_win_condition(Game *game) {
    int white_count = 0;
    int black_count = 0;

    for(int i = 0; i < BOARD_SIZE; i++) {
        for(int j = 0; j < BOARD_SIZE; j++) {
            if(game->board[i][j] == PAWN_WHITE || game->board[i][j] == KING_WHITE)
                white_count++;
            if(game->board[i][j] == PAWN_BLACK || game->board[i][j] == KING_BLACK)
                black_count++;
        }
    }

    if(white_count == 0 && black_count == 0) {
        return GAME_DRAW;
    } else if(white_count == 0) {
        return GAME_LOSE; // Białe przegrały
    } else if(black_count == 0) {
        return GAME_WIN; // Białe wygrały
    }
    return GAME_ONGOING;
}

// Funkcja obsługi rozgrywki – wysyła komunikaty do graczy, odbiera ruchy itp.
void* game_thread(void *arg) {
    Game *game = (Game*)arg;

    // Inicjalnie białe (player1) zaczynają
    game->current_player = 1;

    // Komunikaty wstępne
    char buffer[BUF_SIZE];
    snprintf(buffer, BUF_SIZE, "START BIALY\n"); 
    send(game->player1_socket, buffer, strlen(buffer), 0);

    snprintf(buffer, BUF_SIZE, "START CZARNY\n");
    send(game->player2_socket, buffer, strlen(buffer), 0);

    while(game->is_active) {
        // Odbieramy ruch od aktualnego gracza
        int current_socket = (game->current_player == 1) ? game->player1_socket : game->player2_socket;
        int other_socket = (game->current_player == 1) ? game->player2_socket : game->player1_socket;

        // Informujemy drugiego gracza, że ma czekać
        send(other_socket, "WAIT\n", 5, 0);
        // Informujemy aktualnego gracza, że jest jego kolej
        send(current_socket, "YOUR_TURN\n", 10, 0);

        // Czytamy linijkę tekstu
        memset(buffer, 0, BUF_SIZE);
        int bytes_received = recv(current_socket, buffer, BUF_SIZE - 1, 0);
        if(bytes_received <= 0) {
            // Rozłączenie gracza
            // Obwieszczamy przeciwnikowi wygraną
            snprintf(buffer, BUF_SIZE, "WIN Przeciwnik sie rozlaczyl.\n");
            send(other_socket, buffer, strlen(buffer), 0);
            game->is_active = 0;
            break;
        }

        // Parsujemy komendę
        int from_x, from_y, to_x, to_y;
        if(strncmp(buffer, "MOVE", 4) == 0) {
            // Oczekiwany format: MOVE from_x from_y to_x to_y
            int ret = sscanf(buffer + 4, "%d %d %d %d", &from_x, &from_y, &to_x, &to_y);
            if(ret == 4) {
                // Walidacja
                pthread_mutex_lock(&game->game_mutex);
                int valid = validate_move(game, from_x, from_y, to_x, to_y);
                if(valid) {
                    int was_capture = make_move(game, from_x, from_y, to_x, to_y);
                    // Sprawdzenie promocji na króla (jeśli dotrze do końca)
                    // Białe -> rząd 7, Czarne -> rząd 0
                    for(int i=0; i<BOARD_SIZE; i++){
                        if(game->board[BOARD_SIZE-1][i] == PAWN_WHITE){
                            game->board[BOARD_SIZE-1][i] = KING_WHITE;
                        }
                        if(game->board[0][i] == PAWN_BLACK){
                            game->board[0][i] = KING_BLACK;
                        }
                    }

                    // Sprawdzamy, czy ktoś wygrał
                    GameStatus st = check_win_condition(game);
                    if(st == GAME_WIN) {
                        // Wygrywają białe
                        if(game->current_player == 1) {
                            send(game->player1_socket, "WIN\n", 4, 0);
                            send(game->player2_socket, "LOSE\n", 5, 0);
                        } else {
                            send(game->player2_socket, "WIN\n", 4, 0);
                            send(game->player1_socket, "LOSE\n", 5, 0);
                        }
                        game->is_active = 0;
                        pthread_mutex_unlock(&game->game_mutex);
                        break;
                    } else if(st == GAME_LOSE) {
                        // Przegrywają białe
                        if(game->current_player == 1) {
                            send(game->player1_socket, "LOSE\n", 5, 0);
                            send(game->player2_socket, "WIN\n", 4, 0);
                        } else {
                            send(game->player2_socket, "LOSE\n", 5, 0);
                            send(game->player1_socket, "WIN\n", 4, 0);
                        }
                        game->is_active = 0;
                        pthread_mutex_unlock(&game->game_mutex);
                        break;
                    } else if(st == GAME_DRAW) {
                        // Remis
                        send(game->player1_socket, "DRAW\n", 5, 0);
                        send(game->player2_socket, "DRAW\n", 5, 0);
                        game->is_active = 0;
                        pthread_mutex_unlock(&game->game_mutex);
                        break;
                    }

                    // Jeśli ruch jest poprawny, odsyłamy "OK"
                    send(current_socket, "OK\n", 3, 0);

                    // Jeżeli ruch był poprawny i **nie** było bicia, zmieniamy gracza
                    // (jeśli zaimplementujemy bicie wielokrotne, tu można pozwolić na kolejny ruch).
                    if(!was_capture) {
                        game->current_player = (game->current_player == 1) ? 2 : 1;
                    }

                } else {
                    // Błędny ruch
                    send(current_socket, "ERR Niepoprawny ruch.\n", 22, 0);
                }
                pthread_mutex_unlock(&game->game_mutex);
            } else {
                send(current_socket, "ERR Zly format MOVE.\n", 21, 0);
            }
        } else if(strncmp(buffer, "QUIT", 4) == 0) {
            // Rezygnacja z gry
            snprintf(buffer, BUF_SIZE, "WIN Przeciwnik sie poddal.\n");
            send(other_socket, buffer, strlen(buffer), 0);
            game->is_active = 0;
            break;
        } else {
            // Nieznana komenda
            send(current_socket, "ERR Nieznana komenda.\n", 22, 0);
        }
    }

    // Koniec – zwalniamy gniazda
    close(game->player1_socket);
    close(game->player2_socket);
    pthread_mutex_destroy(&game->game_mutex);
    return NULL;
}

// Funkcja tworzy nową grę w tablicy gier i zwraca wskaźnik
Game* create_game(int p1, int p2) {
    pthread_mutex_lock(&games_mutex);
    for(int i = 0; i < MAX_GAMES; i++) {
        if(!games[i].is_active) {
            games[i].player1_socket = p1;
            games[i].player2_socket = p2;
            init_board(games[i].board);
            games[i].current_player = 1;
            pthread_mutex_init(&games[i].game_mutex, NULL);
            games[i].is_active = 1;
            pthread_mutex_unlock(&games_mutex);
            return &games[i];
        }
    }
    pthread_mutex_unlock(&games_mutex);
    return NULL; // brak miejsca
}

// Funkcja obsługi każdego klienta (wątek)
void* client_thread(void *arg) {
    int client_socket = *(int*)arg;
    free(arg);

    // Sprawdzamy, czy mamy czekającego gracza
    pthread_mutex_lock(&waiting_mutex);
    if(waiting_socket < 0) {
        // Nie ma czekającego – ustawiamy jako czekającego
        waiting_socket = client_socket;
        pthread_mutex_unlock(&waiting_mutex);
        // Czekamy, aż zostaniemy przydzieleni do gry
        // (w rzeczywistości to inny gracz nas odblokuje, gdy przyjdzie)
        // Tu akurat nic nie wysyłamy – można wysłać "WAITING_FOR_PLAYER" itp.
    } else {
        // Już jest ktoś w kolejce – parujemy
        int p1 = waiting_socket;
        int p2 = client_socket;
        waiting_socket = -1;
        pthread_mutex_unlock(&waiting_mutex);

        // Tworzymy rozgrywkę
        Game *game = create_game(p1, p2);
        if(!game) {
            // Brak miejsca na nową grę, rozłączamy
            send(p1, "ERR Serwer pelny\n", 17, 0);
            send(p2, "ERR Serwer pelny\n", 17, 0);
            close(p1);
            close(p2);
            pthread_exit(NULL);
        }

        // Tworzymy wątek, który będzie obsługiwał tę rozgrywkę
        pthread_t tid;
        pthread_create(&tid, NULL, game_thread, (void*)game);
        pthread_detach(tid);
    }

    // Ten wątek kończy się – bo dalej rozgrywką zajmuje się wątek game_thread
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);  // ignoruj błąd "Broken pipe" przy wysyłaniu do zamkniętego gniazda

    int port = PORT_DEFAULT;
    if(argc > 1) {
        port = atoi(argv[1]);
    }

    // Inicjalnie wszystkie gry nieaktywne
    for(int i = 0; i < MAX_GAMES; i++) {
        games[i].is_active = 0;
    }

    int server_socket;
    struct sockaddr_in server_addr;

    // Tworzymy socket
    if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Ustawiamy SO_REUSEADDR, żeby móc szybko włączać/wyłączać serwer
    int opt = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Adres i port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if(listen(server_socket, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Serwer wystartowal na porcie %d\n", port);

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if(client_socket < 0) {
            perror("accept");
            continue;
        }

        // Tworzymy nowy wątek dla klienta
        int *arg = malloc(sizeof(int));
        *arg = client_socket;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, arg);
        pthread_detach(tid);
    }

    close(server_socket);
    return 0;
}

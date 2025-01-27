#ifndef COMMON_H
#define COMMON_H

#define PORT_DEFAULT 12345
#define MAX_CLIENTS 100
#define BUF_SIZE 256

// Rozmiar planszy warcabowej
#define BOARD_SIZE 8

// Status gry
typedef enum {
    GAME_ONGOING,
    GAME_WIN,
    GAME_LOSE,
    GAME_DRAW
} GameStatus;

// Rodzaj pola na planszy
typedef enum {
    EMPTY,
    PAWN_WHITE,
    PAWN_BLACK,
    KING_WHITE,
    KING_BLACK
} FieldType;

#endif

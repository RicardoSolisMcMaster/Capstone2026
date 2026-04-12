#ifndef GAME_DATA_H
#define GAME_DATA_H
#include "tx_api.h"
#include <stdint.h>

extern volatile uint8_t selected_square;
extern volatile uint8_t auto_reset_armed;
extern volatile ULONG game_over_time;
extern volatile uint8_t board[9];
extern volatile uint8_t current_player;
extern volatile uint8_t winner;
extern volatile uint8_t game_over;
extern volatile uint8_t reset_requested;
extern volatile uint8_t winning_indices[3];
extern volatile uint8_t reset_flash_requested;

/* ---------- game mode ---------- */
/* 0 = Tic Tac Toe, 1 = Whack-A-Mole */
extern volatile uint8_t current_mode;
extern volatile uint8_t mode_switch_requested;

/* ---------- Whack-A-Mole ---------- */
extern volatile uint8_t wam_state;
extern volatile uint8_t wam_target_square;
extern volatile uint8_t wam_score;
extern volatile uint8_t wam_misses;
extern volatile uint8_t wam_game_over;
extern volatile uint16_t wam_timeout_ticks;
extern volatile ULONG wam_spawn_time;

#endif

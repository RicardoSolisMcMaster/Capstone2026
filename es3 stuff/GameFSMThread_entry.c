#include "GameFSMThread.h"
#include "hal_data.h"
#include "common_data.h"
#include "game_data.h"
#include <stdint.h>

/* ---------- Shared variable definitions ---------- */
volatile uint8_t selected_square = 255U;
volatile uint8_t board[9] = {0};
volatile uint8_t current_player = 1U;
volatile uint8_t winner = 0U;
volatile uint8_t game_over = 0U;
volatile uint8_t reset_requested = 0U;
volatile uint8_t winning_indices[3] = {255U, 255U, 255U};
volatile uint8_t auto_reset_armed = 0U;
volatile ULONG game_over_time = 0U;
volatile uint8_t reset_flash_requested = 0U;

/* ---------- New shared variables for 2-mode console ---------- */
/* 0 = Tic Tac Toe, 1 = Whack-A-Mole */
volatile uint8_t current_mode = 0U;
volatile uint8_t mode_switch_requested = 0U;

/* Whack-A-Mole data */
volatile uint8_t wam_state = 0U;
volatile uint8_t wam_target_square = 255U;
volatile uint8_t wam_score = 0U;
volatile uint8_t wam_misses = 0U;
volatile uint8_t wam_game_over = 0U;
volatile uint16_t wam_timeout_ticks = 80U;
volatile ULONG wam_spawn_time = 0U;

#define RESET_DEBOUNCE_TICKS 50U
#define GPT_CLOCK_HZ         120000000U

#define MODE_TICTACTOE       0U
#define MODE_WHACKAMOLE      1U

#define WAM_IDLE             0U
#define WAM_SPAWN            1U
#define WAM_WAIT_HIT         2U
#define WAM_HIT_FEEDBACK     3U
#define WAM_MISS_FEEDBACK    4U
#define WAM_CLEAR            5U
#define WAM_GAME_OVER        6U

#define TTT_AUTO_RESET_TICKS 1000U

/* Difficulty */
#define WAM_START_TIMEOUT    120U
#define WAM_MIN_TIMEOUT      25U
#define WAM_TIMEOUT_STEP     5U
#define WAM_MAX_MISSES       3U

/* Small blank gap between moles */
#define WAM_CLEAR_TICKS      10U

/* Short feedback time */
#define WAM_FEEDBACK_TICKS   20U

static uint32_t rng_state = 12345U;

/* ---------- Reset interrupt ---------- */
void reset_button_callback(external_irq_callback_args_t * p_args)
{
    static ULONG last_reset_tick = 0U;
    ULONG now;

    SSP_PARAMETER_NOT_USED(p_args);

    now = tx_time_get();

    if ((now - last_reset_tick) >= RESET_DEBOUNCE_TICKS)
    {
        reset_requested = 1U;
        last_reset_tick = now;
    }
}

/* ---------- Buzzer ---------- */
static void play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    timer_size_t period_counts;
    timer_size_t duty_counts;
    ssp_err_t err;

    if (freq_hz == 0U)
    {
        return;
    }

    period_counts = (timer_size_t)(GPT_CLOCK_HZ / freq_hz);
    duty_counts   = (timer_size_t)(period_counts / 2U);

    err = g_buzzer_timer.p_api->stop(g_buzzer_timer.p_ctrl);
    SSP_PARAMETER_NOT_USED(err);

    err = g_buzzer_timer.p_api->periodSet(g_buzzer_timer.p_ctrl,
                                          period_counts,
                                          TIMER_UNIT_PERIOD_RAW_COUNTS);
    SSP_PARAMETER_NOT_USED(err);

    err = g_buzzer_timer.p_api->dutyCycleSet(g_buzzer_timer.p_ctrl,
                                             duty_counts,
                                             TIMER_PWM_UNIT_RAW_COUNTS,
                                             1);
    SSP_PARAMETER_NOT_USED(err);

    err = g_buzzer_timer.p_api->start(g_buzzer_timer.p_ctrl);
    SSP_PARAMETER_NOT_USED(err);

    R_BSP_SoftwareDelay(duration_ms, BSP_DELAY_UNITS_MILLISECONDS);

    err = g_buzzer_timer.p_api->stop(g_buzzer_timer.p_ctrl);
    SSP_PARAMETER_NOT_USED(err);
}

static void play_valid_move_sound(void)
{
    play_tone(700U, 60U);
    play_tone(1000U, 80U);
}

static void play_invalid_move_sound(void)
{
    play_tone(150U, 250U);
}

static void play_win_sound(void)
{
    play_tone(700U, 200U);
    R_BSP_SoftwareDelay(80U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(900U, 200U);
    R_BSP_SoftwareDelay(80U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(1200U, 500U);
}

static void play_tie_sound(void)
{
    play_tone(600U, 250U);
    R_BSP_SoftwareDelay(120U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(450U, 250U);
    R_BSP_SoftwareDelay(120U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(300U, 400U);
}

static void play_reset_sound(void)
{
    play_tone(1000U, 80U);
    R_BSP_SoftwareDelay(40U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(800U, 80U);
}

static void play_mode_switch_sound(void)
{
    play_tone(900U, 80U);
    R_BSP_SoftwareDelay(40U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(1300U, 100U);
}

static void play_wam_hit_sound(void)
{
    play_tone(1000U, 50U);
    play_tone(1400U, 70U);
}

static void play_wam_miss_sound(void)
{
    play_tone(220U, 180U);
}

static void play_wam_game_over_sound(void)
{
    play_tone(700U, 120U);
    R_BSP_SoftwareDelay(40U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(500U, 120U);
    R_BSP_SoftwareDelay(40U, BSP_DELAY_UNITS_MILLISECONDS);
    play_tone(300U, 220U);
}

/* ---------- Tic Tac Toe helpers ---------- */
static uint8_t check_winner(void)
{
    if ((board[0] != 0U) && (board[0] == board[1]) && (board[1] == board[2]))
    {
        winning_indices[0] = 0U; winning_indices[1] = 1U; winning_indices[2] = 2U;
        return board[0];
    }
    if ((board[3] != 0U) && (board[3] == board[4]) && (board[4] == board[5]))
    {
        winning_indices[0] = 3U; winning_indices[1] = 4U; winning_indices[2] = 5U;
        return board[3];
    }
    if ((board[6] != 0U) && (board[6] == board[7]) && (board[7] == board[8]))
    {
        winning_indices[0] = 6U; winning_indices[1] = 7U; winning_indices[2] = 8U;
        return board[6];
    }

    if ((board[0] != 0U) && (board[0] == board[3]) && (board[3] == board[6]))
    {
        winning_indices[0] = 0U; winning_indices[1] = 3U; winning_indices[2] = 6U;
        return board[0];
    }
    if ((board[1] != 0U) && (board[1] == board[4]) && (board[4] == board[7]))
    {
        winning_indices[0] = 1U; winning_indices[1] = 4U; winning_indices[2] = 7U;
        return board[1];
    }
    if ((board[2] != 0U) && (board[2] == board[5]) && (board[5] == board[8]))
    {
        winning_indices[0] = 2U; winning_indices[1] = 5U; winning_indices[2] = 8U;
        return board[2];
    }

    if ((board[0] != 0U) && (board[0] == board[4]) && (board[4] == board[8]))
    {
        winning_indices[0] = 0U; winning_indices[1] = 4U; winning_indices[2] = 8U;
        return board[0];
    }
    if ((board[2] != 0U) && (board[2] == board[4]) && (board[4] == board[6]))
    {
        winning_indices[0] = 2U; winning_indices[1] = 4U; winning_indices[2] = 6U;
        return board[2];
    }

    winning_indices[0] = 255U;
    winning_indices[1] = 255U;
    winning_indices[2] = 255U;
    return 0U;
}

static uint8_t check_tie(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        if (board[i] == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}

static void reset_ttt_state(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        board[i] = 0U;
    }

    selected_square = 255U;
    current_player = 1U;
    winner = 0U;
    game_over = 0U;
    winning_indices[0] = 255U;
    winning_indices[1] = 255U;
    winning_indices[2] = 255U;
    auto_reset_armed = 0U;
    game_over_time = 0U;
    reset_requested = 0U;
    reset_flash_requested = 1U;
}

static void process_ttt_move(uint8_t move)
{
    uint8_t detected_winner;

    if ((move < 9U) && (game_over == 0U))
    {
        if (board[move] == 0U)
        {
            board[move] = current_player;
            play_valid_move_sound();

            detected_winner = check_winner();

            if (detected_winner != 0U)
            {
                winner = detected_winner;
                game_over = 1U;
                auto_reset_armed = 1U;
                game_over_time = tx_time_get();
                play_win_sound();
            }
            else if (check_tie() != 0U)
            {
                winner = 0U;
                game_over = 1U;
                auto_reset_armed = 1U;
                game_over_time = tx_time_get();
                winning_indices[0] = 255U;
                winning_indices[1] = 255U;
                winning_indices[2] = 255U;
                play_tie_sound();
            }
            else
            {
                if (current_player == 1U)
                {
                    current_player = 2U;
                }
                else
                {
                    current_player = 1U;
                }
            }
        }
        else
        {
            play_invalid_move_sound();
        }

        selected_square = 255U;
    }
}

/* ---------- Whack-A-Mole helpers ---------- */
static uint8_t pseudo_random_square(void)
{
    rng_state ^= (uint32_t) tx_time_get();
    rng_state = (rng_state * 1103515245U) + 12345U;
    return (uint8_t)((rng_state >> 16) % 9U);
}

static void wam_clear_board(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        board[i] = 0U;
    }
}

static void reset_wam_state(void)
{
    selected_square = 255U;
    current_player = 1U;
    winner = 0U;
    game_over = 0U;
    winning_indices[0] = 255U;
    winning_indices[1] = 255U;
    winning_indices[2] = 255U;
    auto_reset_armed = 0U;
    game_over_time = 0U;
    reset_requested = 0U;

    wam_clear_board();

    wam_state = WAM_CLEAR;
    wam_target_square = 255U;
    wam_score = 0U;
    wam_misses = 0U;
    wam_game_over = 0U;
    wam_timeout_ticks = WAM_START_TIMEOUT;
    wam_spawn_time = tx_time_get();

    reset_flash_requested = 1U;
}

static void wam_show_target(uint8_t square)
{
    wam_clear_board();

    if (square < 9U)
    {
        board[square] = 1U;
    }
}

static void wam_show_score(void)
{
    uint32_t i;

    wam_clear_board();

    for (i = 0; (i < wam_score) && (i < 9U); i++)
    {
        board[i] = 1U;
    }
}

static void wam_spawn_target(void)
{
    wam_target_square = pseudo_random_square();
    wam_spawn_time = tx_time_get();
    wam_show_target(wam_target_square);
    wam_state = WAM_WAIT_HIT;
}

static void wam_register_hit(void)
{
    wam_score++;
    play_wam_hit_sound();

    if (wam_timeout_ticks > (WAM_MIN_TIMEOUT + WAM_TIMEOUT_STEP))
    {
        wam_timeout_ticks -= WAM_TIMEOUT_STEP;
    }

    wam_clear_board();
    wam_state = WAM_HIT_FEEDBACK;
    game_over_time = tx_time_get();
}

static void wam_register_miss(void)
{
    wam_misses++;
    play_wam_miss_sound();

    wam_clear_board();

    if (wam_misses >= WAM_MAX_MISSES)
    {
        wam_game_over = 1U;
        wam_state = WAM_GAME_OVER;
        game_over_time = tx_time_get();
        wam_show_score();
        play_wam_game_over_sound();
    }
    else
    {
        wam_state = WAM_MISS_FEEDBACK;
        game_over_time = tx_time_get();
    }
}

static void process_wam(uint8_t move_available, uint8_t move)
{
    switch (wam_state)
    {
        case WAM_IDLE:
            wam_state = WAM_CLEAR;
            wam_spawn_time = tx_time_get();
            break;

        case WAM_SPAWN:
            wam_spawn_target();
            break;

        case WAM_WAIT_HIT:
            if (move_available != 0U)
            {
                if (move == wam_target_square)
                {
                    wam_register_hit();
                }
                else
                {
                    wam_register_miss();
                }

                selected_square = 255U;
            }
            else if ((tx_time_get() - wam_spawn_time) >= wam_timeout_ticks)
            {
                wam_register_miss();
            }
            break;

        case WAM_HIT_FEEDBACK:
            if ((tx_time_get() - game_over_time) >= WAM_FEEDBACK_TICKS)
            {
                wam_state = WAM_CLEAR;
                wam_spawn_time = tx_time_get();
            }
            break;

        case WAM_MISS_FEEDBACK:
            if ((tx_time_get() - game_over_time) >= WAM_FEEDBACK_TICKS)
            {
                wam_state = WAM_CLEAR;
                wam_spawn_time = tx_time_get();
            }
            break;

        case WAM_CLEAR:
            wam_clear_board();

            if ((tx_time_get() - wam_spawn_time) >= WAM_CLEAR_TICKS)
            {
                wam_state = WAM_SPAWN;
            }
            break;

        case WAM_GAME_OVER:
            /* stay here until reset or mode change */
            break;

        default:
            wam_state = WAM_CLEAR;
            wam_spawn_time = tx_time_get();
            break;
    }
}

/* ---------- Mode switching ---------- */
static void switch_mode(uint8_t new_mode)
{
    current_mode = new_mode;
    selected_square = 255U;
    mode_switch_requested = 0U;

    if (new_mode == MODE_TICTACTOE)
    {
        reset_ttt_state();
    }
    else
    {
        reset_wam_state();
    }

    play_mode_switch_sound();
}

/* ---------- Main thread ---------- */
void GameFSMThread_entry(void)
{
    UINT status;
    uint8_t move = 255U;
    uint8_t move_available;
    ssp_err_t err;

    err = g_external_irq0.p_api->open(g_external_irq0.p_ctrl, g_external_irq0.p_cfg);
    if (err != SSP_SUCCESS)
    {
        while (1)
        {
        }
    }

    err = g_buzzer_timer.p_api->open(g_buzzer_timer.p_ctrl, g_buzzer_timer.p_cfg);
    if (err != SSP_SUCCESS)
    {
        while (1)
        {
        }
    }

    current_mode = MODE_TICTACTOE;
    reset_ttt_state();

    while (1)
    {
        if (reset_requested == 1U)
        {
            if (current_mode == MODE_TICTACTOE)
            {
                reset_ttt_state();
                play_reset_sound();
            }
            else
            {
                reset_wam_state();
                play_reset_sound();
            }
        }

        if (mode_switch_requested == 1U)
        {
            if (current_mode == MODE_TICTACTOE)
            {
                switch_mode(MODE_WHACKAMOLE);
            }
            else
            {
                switch_mode(MODE_TICTACTOE);
            }
        }

        if ((current_mode == MODE_TICTACTOE) &&
            (auto_reset_armed == 1U) &&
            ((tx_time_get() - game_over_time) >= TTT_AUTO_RESET_TICKS))
        {
            reset_ttt_state();
            play_reset_sound();
        }

        status = tx_semaphore_get(&g_move_semaphore, 1);

        if ((status == TX_SUCCESS) && (selected_square < 9U))
        {
            move = selected_square;
            move_available = 1U;
        }
        else
        {
            move = 255U;
            move_available = 0U;
        }

        if (current_mode == MODE_TICTACTOE)
        {
            if (move_available != 0U)
            {
                process_ttt_move(move);
            }
        }
        else
        {
            process_wam(move_available, move);
        }
    }
}

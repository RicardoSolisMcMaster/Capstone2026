#include "DisplayThread.h"
#include "hal_data.h"
#include "game_data.h"
#include <stdint.h>

/* ---------- Mode constants ---------- */
#define MODE_TICTACTOE    0U
#define MODE_WHACKAMOLE   1U

/* ---------- Whack-A-Mole state constants ---------- */
#define WAM_IDLE          0U
#define WAM_SPAWN         1U
#define WAM_WAIT_HIT      2U
#define WAM_HIT_FEEDBACK  3U
#define WAM_MISS_FEEDBACK 4U
#define WAM_CLEAR         5U
#define WAM_GAME_OVER     6U

/* ---------- External LED pins ---------- */
#define LED1A   IOPORT_PORT_03_PIN_04
#define LED1B   IOPORT_PORT_01_PIN_04

#define LED2A   IOPORT_PORT_01_PIN_05
#define LED2B   IOPORT_PORT_01_PIN_06

#define LED3A   IOPORT_PORT_01_PIN_12
#define LED3B   IOPORT_PORT_02_PIN_02

#define LED4A   IOPORT_PORT_03_PIN_01
#define LED4B   IOPORT_PORT_03_PIN_02

#define LED5A   IOPORT_PORT_03_PIN_03
#define LED5B   IOPORT_PORT_05_PIN_06

#define LED6A   IOPORT_PORT_05_PIN_07
#define LED6B   IOPORT_PORT_06_PIN_08

#define LED7A   IOPORT_PORT_06_PIN_13
#define LED7B   IOPORT_PORT_06_PIN_14

#define LED8A   IOPORT_PORT_03_PIN_05
#define LED8B   IOPORT_PORT_03_PIN_06

#define LED9A   IOPORT_PORT_01_PIN_11
#define LED9B   IOPORT_PORT_00_PIN_09

/* ---------- RGB indicator ---------- */
#define RGB_RED    IOPORT_PORT_02_PIN_05
#define RGB_BLUE   IOPORT_PORT_02_PIN_06

static const ioport_port_pin_t led_pins[9][2] =
{
    { LED1A, LED1B },
    { LED2A, LED2B },
    { LED3A, LED3B },
    { LED4A, LED4B },
    { LED5A, LED5B },
    { LED6A, LED6B },
    { LED7A, LED7B },
    { LED8A, LED8B },
    { LED9A, LED9B }
};

static void config_led_pins(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        g_ioport.p_api->pinCfg(led_pins[i][0], IOPORT_CFG_PORT_DIRECTION_OUTPUT);
        g_ioport.p_api->pinCfg(led_pins[i][1], IOPORT_CFG_PORT_DIRECTION_OUTPUT);
    }

    g_ioport.p_api->pinCfg(RGB_RED, IOPORT_CFG_PORT_DIRECTION_OUTPUT);
    g_ioport.p_api->pinCfg(RGB_BLUE, IOPORT_CFG_PORT_DIRECTION_OUTPUT);
}

static void all_leds_off(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_LOW);
        g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_LOW);
    }
}

static void all_leds_on(void)
{
    uint32_t i;

    for (i = 0; i < 9U; i++)
    {
        g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
        g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_HIGH);
    }
}

static void rgb_off(void)
{
    g_ioport.p_api->pinWrite(RGB_RED, IOPORT_LEVEL_LOW);
    g_ioport.p_api->pinWrite(RGB_BLUE, IOPORT_LEVEL_LOW);
}

static void rgb_red_on(void)
{
    g_ioport.p_api->pinWrite(RGB_RED, IOPORT_LEVEL_HIGH);
    g_ioport.p_api->pinWrite(RGB_BLUE, IOPORT_LEVEL_LOW);
}

static void rgb_blue_on(void)
{
    g_ioport.p_api->pinWrite(RGB_RED, IOPORT_LEVEL_LOW);
    g_ioport.p_api->pinWrite(RGB_BLUE, IOPORT_LEVEL_HIGH);
}

static void rgb_purple_on(void)
{
    g_ioport.p_api->pinWrite(RGB_RED, IOPORT_LEVEL_HIGH);
    g_ioport.p_api->pinWrite(RGB_BLUE, IOPORT_LEVEL_HIGH);
}

static uint8_t is_winning_square(uint8_t index)
{
    if (winning_indices[0] == index) return 1U;
    if (winning_indices[1] == index) return 1U;
    if (winning_indices[2] == index) return 1U;
    return 0U;
}

static void display_tictactoe(uint8_t blink_state)
{
    uint32_t i;

    if (game_over == 0U)
    {
        if (current_player == 1U)
        {
            rgb_blue_on();
        }
        else
        {
            rgb_red_on();
        }
    }
    else
    {
        if (winner == 1U)
        {
            if (blink_state != 0U) { rgb_blue_on(); } else { rgb_off(); }
        }
        else if (winner == 2U)
        {
            if (blink_state != 0U) { rgb_red_on(); } else { rgb_off(); }
        }
        else
        {
            if (blink_state != 0U) { rgb_purple_on(); } else { rgb_off(); }
        }
    }

    all_leds_off();

    if (game_over == 0U)
    {
        for (i = 0; i < 9U; i++)
        {
            if (board[i] == 1U)
            {
                g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
            }
            else if (board[i] == 2U)
            {
                g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_HIGH);
            }
        }
    }
    else
    {
        if (winner == 0U)
        {
            for (i = 0; i < 9U; i++)
            {
                if (board[i] == 1U)
                {
                    g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
                }
                else if (board[i] == 2U)
                {
                    g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_HIGH);
                }
            }
        }
        else
        {
            for (i = 0; i < 9U; i++)
            {
                if (board[i] == 1U)
                {
                    if ((winner == 1U) && (is_winning_square((uint8_t) i) != 0U))
                    {
                        if (blink_state != 0U)
                        {
                            g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
                        }
                    }
                    else
                    {
                        g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
                    }
                }
                else if (board[i] == 2U)
                {
                    if ((winner == 2U) && (is_winning_square((uint8_t) i) != 0U))
                    {
                        if (blink_state != 0U)
                        {
                            g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_HIGH);
                        }
                    }
                    else
                    {
                        g_ioport.p_api->pinWrite(led_pins[i][1], IOPORT_LEVEL_HIGH);
                    }
                }
            }
        }
    }
}

static void display_whack_a_mole(uint8_t blink_state)
{
    uint32_t i;

    if (wam_state == WAM_GAME_OVER)
    {
        if (blink_state != 0U)
        {
            rgb_red_on();
        }
        else
        {
            rgb_off();
        }
    }
    else
    {
        if (wam_misses == 0U)
        {
            rgb_blue_on();
        }
        else if (wam_misses == 1U)
        {
            rgb_purple_on();
        }
        else
        {
            rgb_red_on();
        }
    }

    all_leds_off();

    for (i = 0; i < 9U; i++)
    {
        if (board[i] == 1U)
        {
            g_ioport.p_api->pinWrite(led_pins[i][0], IOPORT_LEVEL_HIGH);
        }
    }
}

void DisplayThread_entry(void)
{
    uint8_t blink_state = 0U;
    uint32_t blink_counter = 0U;

    config_led_pins();
    all_leds_off();
    rgb_off();

    while (1)
    {
        if (reset_flash_requested == 1U)
        {
            all_leds_on();
            rgb_off();
            tx_thread_sleep(10);

            all_leds_off();
            rgb_off();
            tx_thread_sleep(10);

            reset_flash_requested = 0U;
        }

        blink_counter++;
        if (blink_counter >= 4U)
        {
            blink_state = !blink_state;
            blink_counter = 0U;
        }

        if (current_mode == MODE_TICTACTOE)
        {
            display_tictactoe(blink_state);
        }
        else
        {
            display_whack_a_mole(blink_state);
        }

        tx_thread_sleep(50);
    }
}

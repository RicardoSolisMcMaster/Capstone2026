#include "InputScanThread.h"
#include "hal_data.h"
#include "game_data.h"
#include <stdint.h>
#include "common_data.h"
#include "GameFSMThread.h"

/* ---------- Button matrix pins ---------- */
/* Rows = outputs */
#define ROW1    IOPORT_PORT_03_PIN_10
#define ROW2    IOPORT_PORT_03_PIN_11
#define ROW3    IOPORT_PORT_03_PIN_12

/* Columns = inputs with pull-up */
#define COL1    IOPORT_PORT_06_PIN_03
#define COL2    IOPORT_PORT_06_PIN_04
#define COL3    IOPORT_PORT_06_PIN_05

/* ---------- Mode button ---------- */
#define MODE_BTN   IOPORT_PORT_02_PIN_07

static const ioport_port_pin_t row_pins[3] =
{
    ROW1, ROW2, ROW3
};

static const ioport_port_pin_t col_pins[3] =
{
    COL1, COL2, COL3
};

static void config_button_pins(void)
{
    g_ioport.p_api->pinCfg(ROW1, IOPORT_CFG_PORT_DIRECTION_OUTPUT);
    g_ioport.p_api->pinCfg(ROW2, IOPORT_CFG_PORT_DIRECTION_OUTPUT);
    g_ioport.p_api->pinCfg(ROW3, IOPORT_CFG_PORT_DIRECTION_OUTPUT);

    g_ioport.p_api->pinCfg(COL1, IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE);
    g_ioport.p_api->pinCfg(COL2, IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE);
    g_ioport.p_api->pinCfg(COL3, IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE);

    g_ioport.p_api->pinCfg(MODE_BTN, IOPORT_CFG_PORT_DIRECTION_INPUT | IOPORT_CFG_PULLUP_ENABLE);
}

static void all_rows_high(void)
{
    g_ioport.p_api->pinWrite(ROW1, IOPORT_LEVEL_HIGH);
    g_ioport.p_api->pinWrite(ROW2, IOPORT_LEVEL_HIGH);
    g_ioport.p_api->pinWrite(ROW3, IOPORT_LEVEL_HIGH);
}

static uint8_t scan_matrix_once(void)
{
    ioport_level_t col_state;
    uint32_t row;
    uint32_t col;

    for (row = 0; row < 3; row++)
    {
        all_rows_high();
        g_ioport.p_api->pinWrite(row_pins[row], IOPORT_LEVEL_LOW);
        tx_thread_sleep(1);

        for (col = 0; col < 3; col++)
        {
            g_ioport.p_api->pinRead(col_pins[col], &col_state);

            if (col_state == IOPORT_LEVEL_LOW)
            {
                all_rows_high();
                return (uint8_t)((row * 3U) + col);
            }
        }
    }

    all_rows_high();
    return 255U;
}

static void wait_for_button_release(void)
{
    while (scan_matrix_once() != 255U)
    {
        tx_thread_sleep(1);
    }
}

static void check_mode_button(void)
{
    static uint8_t last_mode_state = 1U;
    ioport_level_t mode_level;

    g_ioport.p_api->pinRead(MODE_BTN, &mode_level);

    if ((mode_level == IOPORT_LEVEL_LOW) && (last_mode_state == 1U))
    {
        mode_switch_requested = 1U;
    }

    if (mode_level == IOPORT_LEVEL_LOW)
    {
        last_mode_state = 0U;
    }
    else
    {
        last_mode_state = 1U;
    }
}

void InputScanThread_entry(void)
{
    uint8_t square;

    config_button_pins();
    all_rows_high();

    while (1)
    {
        check_mode_button();

        square = scan_matrix_once();

        if (square < 9U)
        {
            selected_square = square;
            tx_semaphore_put(&g_move_semaphore);
            wait_for_button_release();
        }

        tx_thread_sleep(1);
    }
}

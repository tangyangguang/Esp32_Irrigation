#include "board/board_init.h"

#include "board_pins.h"
#include "drivers/input_monitor.h"
#include "drivers/output_driver.h"
#include "stc8h_gpio.h"
#include "stc8h_interrupt.h"
#include "stc8h_sfr.h"
#include "stc8h_timer.h"
#include "stc8h_wdt.h"

static volatile stc8h_u32 g_millis;

STC8H_INTERRUPT(timer0_isr, STC8H_VECTOR_TIMER0)
{
    stc8h_timer_clear_flag(STC8H_TIMER0);
    ++g_millis;
}

static void init_dip_input(stc8h_u8 port, stc8h_u8 pin)
{
    stc8h_gpio_set_mode(port, pin, STC8H_GPIO_MODE_INPUT_ONLY);
}

void board_init_safe_state(void)
{
    output_driver_safe_off();
}

void board_init_used_peripherals(void)
{
    stc8h_gpio_set_mode(BOARD_RS485_DIR_PORT, BOARD_RS485_DIR_PIN, STC8H_GPIO_MODE_PUSH_PULL);
    P4 &= (stc8h_u8)~BOARD_RS485_DIR_MASK;

    init_dip_input(BOARD_DIP0_PORT, BOARD_DIP0_PIN);
    init_dip_input(BOARD_DIP1_PORT, BOARD_DIP1_PIN);
    init_dip_input(BOARD_DIP2_PORT, BOARD_DIP2_PIN);
    init_dip_input(BOARD_DIP3_PORT, BOARD_DIP3_PIN);

    output_driver_init();
    input_monitor_init();

    if (stc8h_timer_init_1ms(STC8H_TIMER0) == STC8H_OK) {
        stc8h_timer_enable_interrupt(STC8H_TIMER0);
        stc8h_interrupt_enable_global();
        stc8h_timer_start(STC8H_TIMER0);
    }

    stc8h_wdt_enable(STC8H_WDT_SCALE_256, 1u);
}

stc8h_u8 board_station_address(void)
{
    stc8h_u8 addr = 0u;
    addr |= (stc8h_gpio_read(BOARD_DIP0_PORT, BOARD_DIP0_PIN) != 0u) ? 0x01u : 0u;
    addr |= (stc8h_gpio_read(BOARD_DIP1_PORT, BOARD_DIP1_PIN) != 0u) ? 0x02u : 0u;
    addr |= (stc8h_gpio_read(BOARD_DIP2_PORT, BOARD_DIP2_PIN) != 0u) ? 0x04u : 0u;
    addr |= (stc8h_gpio_read(BOARD_DIP3_PORT, BOARD_DIP3_PIN) != 0u) ? 0x08u : 0u;
    return (addr == 0u) ? 1u : addr;
}

stc8h_u32 board_uptime_sec(void)
{
    return g_millis / 1000UL;
}

stc8h_u32 board_millis(void)
{
    return g_millis;
}

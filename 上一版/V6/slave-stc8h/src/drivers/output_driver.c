#include "drivers/output_driver.h"

#include "board_pins.h"
#include "stc8h_gpio.h"
#include "stc8h_pwm.h"

#define PWM_PRESCALER 10u
#define PWM_PERIOD_TICKS 1023u

typedef struct {
    stc8h_u8 group;
    stc8h_u8 channel;
    stc8h_u8 pin_select;
    stc8h_u8 port;
    stc8h_u8 pin;
} board_pwm_channel_t;

static const board_pwm_channel_t g_pwm[] = {
    { STC8H_PWM_GROUP_A, STC8H_PWM_CHANNEL_1, STC8H_PWM_PIN_PWM1_P20, 2u, 0u },
    { STC8H_PWM_GROUP_A, STC8H_PWM_CHANNEL_2, STC8H_PWM_PIN_PWM2_P22, 2u, 2u },
    { STC8H_PWM_GROUP_A, STC8H_PWM_CHANNEL_3, STC8H_PWM_PIN_PWM3_P24, 2u, 4u },
    { STC8H_PWM_GROUP_A, STC8H_PWM_CHANNEL_4, STC8H_PWM_PIN_PWM4_P26, 2u, 6u },
    { STC8H_PWM_GROUP_B, STC8H_PWM_CHANNEL_5, STC8H_PWM_PIN_PWM5_P00, 0u, 0u },
    { STC8H_PWM_GROUP_B, STC8H_PWM_CHANNEL_6, STC8H_PWM_PIN_PWM6_P01, 0u, 1u },
    { STC8H_PWM_GROUP_B, STC8H_PWM_CHANNEL_7, STC8H_PWM_PIN_PWM7_P02, 0u, 2u },
    { STC8H_PWM_GROUP_B, STC8H_PWM_CHANNEL_8, STC8H_PWM_PIN_PWM8_P03, 0u, 3u },
};

void output_driver_safe_off(void)
{
    stc8h_u8 i;
    for (i = 0u; i < (stc8h_u8)(sizeof(g_pwm) / sizeof(g_pwm[0])); ++i) {
        stc8h_gpio_set_mode(g_pwm[i].port, g_pwm[i].pin, STC8H_GPIO_MODE_PUSH_PULL);
        stc8h_gpio_write(g_pwm[i].port, g_pwm[i].pin, 0u);
    }
    stc8h_gpio_set_mode(BOARD_DRY_OUT1_PORT, BOARD_DRY_OUT1_PIN, STC8H_GPIO_MODE_PUSH_PULL);
    stc8h_gpio_write(BOARD_DRY_OUT1_PORT, BOARD_DRY_OUT1_PIN, 0u);
}

void output_driver_init(void)
{
    stc8h_u8 i;

    (void)stc8h_pwm_set_prescaler(STC8H_PWM_GROUP_A, PWM_PRESCALER);
    (void)stc8h_pwm_set_period(STC8H_PWM_GROUP_A, PWM_PERIOD_TICKS);
    (void)stc8h_pwm_set_prescaler(STC8H_PWM_GROUP_B, PWM_PRESCALER);
    (void)stc8h_pwm_set_period(STC8H_PWM_GROUP_B, PWM_PERIOD_TICKS);

    for (i = 0u; i < (stc8h_u8)(sizeof(g_pwm) / sizeof(g_pwm[0])); ++i) {
        stc8h_gpio_set_mode(g_pwm[i].port, g_pwm[i].pin, STC8H_GPIO_MODE_PUSH_PULL);
        (void)stc8h_pwm_init_channel(g_pwm[i].group, g_pwm[i].channel, g_pwm[i].pin_select);
        (void)stc8h_pwm_set_duty(g_pwm[i].group, g_pwm[i].channel, 0u);
        (void)stc8h_pwm_disable(g_pwm[i].group, g_pwm[i].channel);
    }
    output_driver_set_pump(0u);
}

stc8h_status_t output_driver_set_valve(stc8h_u8 valve_no, stc8h_u16 duty_permille)
{
    const board_pwm_channel_t *channel;
    stc8h_u16 duty_ticks;

    if (valve_no == 0u || valve_no > (stc8h_u8)(sizeof(g_pwm) / sizeof(g_pwm[0]))) {
        return STC8H_ERROR;
    }
    if (duty_permille > 1000u) {
        duty_permille = 1000u;
    }
    channel = &g_pwm[valve_no - 1u];
    duty_ticks = (stc8h_u16)(((stc8h_u32)PWM_PERIOD_TICKS * (stc8h_u32)duty_permille) / 1000UL);
    if (stc8h_pwm_set_duty(channel->group, channel->channel, duty_ticks) != STC8H_OK) {
        return STC8H_ERROR;
    }
    if (duty_ticks == 0u) {
        return stc8h_pwm_disable(channel->group, channel->channel);
    }
    return stc8h_pwm_enable(channel->group, channel->channel);
}

void output_driver_close_valve(stc8h_u8 valve_no)
{
    (void)output_driver_set_valve(valve_no, 0u);
}

void output_driver_close_all_valves(void)
{
    stc8h_u8 i;

    for (i = 1u; i <= (stc8h_u8)(sizeof(g_pwm) / sizeof(g_pwm[0])); ++i) {
        output_driver_close_valve(i);
    }
}

void output_driver_set_pump(stc8h_u8 enabled)
{
    stc8h_gpio_write(BOARD_DRY_OUT1_PORT, BOARD_DRY_OUT1_PIN, enabled ? 1u : 0u);
}


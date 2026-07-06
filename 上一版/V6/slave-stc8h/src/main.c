#include "board/board_init.h"
#include "app/task_runner.h"
#include "comm/command_handler.h"
#include "drivers/input_monitor.h"
#include "stc8h_wdt.h"

void main(void)
{
    board_init_safe_state();
    board_init_used_peripherals();
    task_runner_init();
    command_handler_init();

    while (1) {
        stc8h_wdt_feed();
        input_monitor_handle();
        task_runner_handle();
        command_handler_handle();
    }
}

#ifndef IRRIGATION_STATUS_REPORTER_H
#define IRRIGATION_STATUS_REPORTER_H

#include "stc8h_config.h"

#define IRR_STATION_STATE_BOOTING 1u
#define IRR_STATION_STATE_IDLE    2u
#define IRR_STATION_STATE_RUNNING 3u
#define IRR_STATION_STATE_STOPPING 4u
#define IRR_STATION_STATE_FAULT   5u
#define IRR_TASK_STATE_NONE    0u
#define IRR_TASK_STATE_STARTING 1u
#define IRR_TASK_STATE_RUNNING 2u
#define IRR_TASK_STATE_STOPPING 3u
#define IRR_TASK_STATE_COMPLETED 4u
#define IRR_TASK_STATE_STOPPED_BY_USER 5u
#define IRR_TASK_STATE_STOPPED_BY_FAULT 6u
#define IRR_FAULT_NONE         0u
#define IRR_FAULT_LOW_LEVEL    1u
#define IRR_RESULT_NONE        0u
#define IRR_RESULT_COMPLETED   1u
#define IRR_RESULT_STOPPED_BY_USER 2u
#define IRR_RESULT_STOPPED_BY_FAULT 3u

void status_reporter_get_status(stc8h_u8 *payload, stc8h_u8 *len);
void status_reporter_get_info(stc8h_u8 *payload, stc8h_u8 *len);
void status_reporter_get_inputs(stc8h_u8 *payload, stc8h_u8 *len);
void status_reporter_get_last_result(stc8h_u8 *payload, stc8h_u8 *len);

#endif

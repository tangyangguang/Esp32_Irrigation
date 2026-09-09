#pragma once
#include <sdkconfig.h>
#if !defined(CONFIG_MBEDTLS_HAVE_TIME_DATE) || !CONFIG_MBEDTLS_HAVE_TIME_DATE
#error Irrigation requires actual certificate date verification
#endif
#if !CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED || !CONFIG_MQTT_USE_CORE_0 || defined(CONFIG_MQTT_USE_CORE_1)
#error Irrigation requires the controlled ESP32 MQTT core placement
#endif

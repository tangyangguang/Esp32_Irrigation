#pragma once

// Real MQTT credentials and CA material live only in
// firmware/local_private/irrigation_iot_private.h (Git ignored). The private
// header may override every macro below before this file supplies safe defaults.
#if __has_include("irrigation_iot_private.h")
#include "irrigation_iot_private.h"
#endif

#ifndef IRRIGATION_IOT_MQTT_HOST
#define IRRIGATION_IOT_MQTT_HOST ""
#endif

#ifndef IRRIGATION_IOT_MQTT_PORT
#define IRRIGATION_IOT_MQTT_PORT 8883
#endif

#ifndef IRRIGATION_IOT_MQTT_USERNAME
#define IRRIGATION_IOT_MQTT_USERNAME ""
#endif

#ifndef IRRIGATION_IOT_MQTT_PASSWORD
#define IRRIGATION_IOT_MQTT_PASSWORD ""
#endif

#ifndef IRRIGATION_IOT_MQTT_CA_PEM
#define IRRIGATION_IOT_MQTT_CA_PEM ""
#endif

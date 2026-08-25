#pragma once

// Copy this file to ../local_private/IrrigationMqttPrivate.h and replace every
// placeholder locally. The local_private directory is ignored by Git.
#define IRRIGATION_MQTT_ENABLED 1
#define IRRIGATION_MQTT_BROKER_URI "mqtts://broker.example.invalid:8883"
#define IRRIGATION_MQTT_DEVICE_ID "irrigation-real-001"
#define IRRIGATION_MQTT_CLIENT_ID "irrigation-real-001-client"
#define IRRIGATION_MQTT_USERNAME "replace-with-device-username"
#define IRRIGATION_MQTT_PASSWORD "replace-with-device-password"
#define IRRIGATION_MQTT_DEFINITION_SHA256 \
    "e09e0e9e649d6b7d618c3ab849fd0c3695e557c9e2e920fedf91b55e91cd92f8"

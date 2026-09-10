#pragma once
constexpr int ESP_OK = 0;
inline int esp_task_wdt_add(void*) { return ESP_OK; }
inline int esp_task_wdt_reset() { return ESP_OK; }

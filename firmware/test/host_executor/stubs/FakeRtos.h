#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
using TickType_t = uint32_t;
using TaskHandle_t = void*;
inline thread_local TaskHandle_t currentTask = reinterpret_cast<void*>(1);
constexpr int pdTRUE = 1, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(value) (value)
struct StaticSemaphore_t { std::mutex mutex; std::condition_variable cv; bool available = false; };
using SemaphoreHandle_t = StaticSemaphore_t*;
inline auto xSemaphoreCreateMutexStatic(StaticSemaphore_t* s) { s->available = true; return s; }
inline auto xSemaphoreCreateBinaryStatic(StaticSemaphore_t* s) { return s; }
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
    std::unique_lock<std::mutex> lock(s->mutex);
    s->cv.wait(lock, [&] { return s->available; }); s->available = false; return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t s) {
    std::lock_guard<std::mutex> lock(s->mutex); s->available = true; s->cv.notify_one(); return pdTRUE;
}
struct StaticQueue_t { std::mutex mutex; std::condition_variable cv; void* value = nullptr; };
using QueueHandle_t = StaticQueue_t*;
inline auto xQueueCreateStatic(int, int, uint8_t*, StaticQueue_t* q) { return q; }
inline int xQueueSend(QueueHandle_t q, const void* data, TickType_t) {
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->value) return 0;
    std::memcpy(&q->value, data, sizeof(void*)); q->cv.notify_one(); return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, void* out, TickType_t timeout) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if (!q->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return q->value != nullptr; })) return 0;
    std::memcpy(out, &q->value, sizeof(void*)); q->value = nullptr; return pdTRUE;
}
inline auto xTaskGetCurrentTaskHandle() { return currentTask; }
inline int xTaskCreatePinnedToCore(void (*run)(void*), const char*, unsigned, void* context, int, TaskHandle_t* out, int) {
    *out = reinterpret_cast<void*>(2);
    std::thread([=] { currentTask = reinterpret_cast<void*>(2); run(context); }).detach(); return pdPASS;
}

#pragma once
#include "FakeRtos.h"
inline const auto bootTime = std::chrono::steady_clock::now();
inline uint32_t millis() { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bootTime).count()); }

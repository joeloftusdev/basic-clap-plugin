#pragma once

#include <mutex>
#include <algorithm>

using Mutex = std::mutex;
#define MutexAcquire(mutex) (mutex).lock()
#define MutexRelease(mutex) (mutex).unlock()
#define MutexInitialise(mutex)
#define MutexDestroy(mutex)

static float FloatClamp01(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}